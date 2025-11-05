<?php


// Context window length for hashing around the quote.
if (!defined('XRN_CTX_WIN')) define('XRN_CTX_WIN', 128);

/**
 * Reattach all anchors on a node using quote-only matching with disambiguation.
 */
function xrnote_reanchor_node($node) {
  if (empty($node) || empty($node->nid)) return;

  // Single-lang body. No back-compat branches.
  $body = isset($node->body['und'][0]['value']) ? $node->body['und'][0]['value'] : '';
  if ($body === '') return;

  $anchors = xrnote_load_anchors($node->nid);
  if (!$anchors) return;

  // Group by exact quote to avoid rescanning the body for each anchor.
  $by_exact = array();
  foreach ($anchors as $a) {
    $sel = is_array($a['selector']) ? $a['selector'] : json_decode($a['selector'], TRUE);
    $exact = isset($sel['quote']['exact']) ? $sel['quote']['exact'] : '';
    if ($exact === '') continue;
    $by_exact[$exact][] = array('aid' => $a['aid'], 'sel' => $sel);
  }

  foreach ($by_exact as $exact => $items) {
    $positions = _xrnote_all_occurrences($body, $exact);
    if (!$positions) {
      foreach ($items as $it) {
        watchdog('xrnote',
          'Anchor unresolved: no occurrence for exact quote on node %nid (aid=%aid).',
          array('%nid'=>$node->nid, '%aid'=>$it['aid']), WATCHDOG_WARNING);
      }
      continue;
    }
    foreach ($items as $it) {
      $sel = $it['sel'];
      $pos = _xrnote_pick_best($body, $positions, $exact, $sel);
      if ($pos < 0) {
        watchdog('xrnote',
          'Anchor unresolved: ambiguous match on node %nid (aid=%aid).',
          array('%nid'=>$node->nid, '%aid'=>$it['aid']), WATCHDOG_WARNING);
        continue;
      }
      $sel['pos'] = array('start'=>$pos, 'end'=>$pos + strlen($exact));
      db_update('xrnote_anchor')
        ->fields(array('selector'=>json_encode($sel), 'changed'=>REQUEST_TIME))
        ->condition('aid', $it['aid'])
        ->execute();
    }
  }
}

// xrnote.anchor.inc
function _xrnote_map_offsets($patches, $start, $end) {
  // Simple shift accumulator. Works when patches don't touch the range.
  // If a patch overlaps [start,end), return FALSE so quote-fallback runs.
  if (!is_array($patches)) return FALSE;
  $s = (int) $start; $e = (int) $end; $shift = 0;

  foreach ($patches as $p) {
    // Accept both real dmp_Patch and array-like with properties.
    $pstart = isset($p->start1) ? $p->start1 : (isset($p['start1']) ? $p['start1'] : 0);
    $plen1  = isset($p->length1) ? $p->length1 : (isset($p['length1']) ? $p['length1'] : 0);
    $plen2  = isset($p->length2) ? $p->length2 : (isset($p['length2']) ? $p['length2'] : 0);

    $pend   = $pstart + $plen1;
    $delta  = $plen2 - $plen1;

    if ($pend <= $s) { // patch entirely before our span
      $shift += $delta;
      continue;
    }
    if ($pstart >= $e) { // patch entirely after our span
      break;
    }
    // Overlap with our span: too ambiguous for index math; let quote finder handle it.
    return FALSE;
  }

  return array($s + $shift, $e + $shift);
}

// Stubs for mapping and quote matching.

function _xrnote_find_quote($text, $q) {
  // $q = array('exact'=>'...', 'prefix'=>'...', 'suffix'=>'...')
  if (empty($q['exact'])) return FALSE;
  $pos = strpos($text, $q['exact']);
  if ($pos === FALSE) return FALSE;
  return array($pos, $pos + strlen($q['exact']));
}

/**
 * Return all start offsets where $needle occurs in $haystack.
 */
function _xrnote_all_occurrences($haystack, $needle) {
  $out = array();
  if ($needle === '') return $out;
  $n = strlen($needle);
  $off = 0;
  while (TRUE) {
    $p = strpos($haystack, $needle, $off);
    if ($p === FALSE) break;
    $out[] = $p;
    $off = $p + $n;
  }
  return $out;
}

/**
 * Choose best candidate using prefix/suffix and left/right context hashes.
 * Returns start offset or -1.
 */
function _xrnote_pick_best($body, array $positions, $exact, array $sel) {
  $pre = isset($sel['quote']['prefix']) ? $sel['quote']['prefix'] : '';
  $suf = isset($sel['quote']['suffix']) ? $sel['quote']['suffix'] : '';
  $win = isset($sel['ctx']['win']) ? (int)$sel['ctx']['win'] : XRN_CTX_WIN;
  $hL  = isset($sel['ctx']['L']) ? $sel['ctx']['L'] : '';
  $hR  = isset($sel['ctx']['R']) ? $sel['ctx']['R'] : '';
  $hintLine = isset($sel['hint']['line']) ? (int)$sel['hint']['line'] : NULL;

  $best = -1;
  $scoreBest = -1;
  $elen = strlen($exact);

  foreach ($positions as $p) {
    $score = 0;

    if ($pre !== '') {
      $plen = strlen($pre);
      $seg = ($plen <= $p) ? substr($body, $p - $plen, $plen) : '';
      if ($seg === $pre) $score += 4;
    }

    if ($suf !== '') {
      $slen = strlen($suf);
      $seg = substr($body, $p + $elen, $slen);
      if ($seg === $suf) $score += 4;
    }

    if ($hL !== '' || $hR !== '') {
      list($cL, $cR) = _xrnote_ctx_hash_pair($body, $p, $p + $elen, $win);
      if ($hL !== '' && $cL === $hL) $score += 1;
      if ($hR !== '' && $cR === $hR) $score += 1;
    }

    if ($hintLine !== NULL) {
      $line = substr_count(substr($body, 0, $p), "\n") + 1;  // safe across PHP versions
      $score += max(0, 1 - min(1, abs($line - $hintLine) / 1000.0));
    }

    if ($score > $scoreBest) {
      $scoreBest = $score;
      $best = $p;
    }
  }
  return $best;
}

/**
 * Return [Lhash, Rhash] for windows around [start,end).
 */
function _xrnote_ctx_hash_pair($body, $start, $end, $win) {
  $Lstart = max(0, $start - $win);
  $Llen   = $start - $Lstart;
  $Rstart = $end;
  $Rlen   = $win;

  $L = ($Llen > 0) ? substr($body, $Lstart, $Llen) : '';
  $R = substr($body, $Rstart, $Rlen);

  return array(
    substr(sha1($L, FALSE), 0, 16),
    substr(sha1($R, FALSE), 0, 16)
  );
}

function xrnote_load_anchors($parent_nid) {
  $out = array();
  $q = db_query("SELECT aid, uuid, note_nid, selector, weight FROM {xrnote_anchor} WHERE parent_nid = :n ORDER BY weight, aid", array(':n'=>$parent_nid));
  foreach ($q as $row) {
    $out[] = array(
      'aid' => (int)$row->aid,
      'uuid' => $row->uuid,
      'note_nid' => (int)$row->note_nid,
      'selector' => json_decode($row->selector, TRUE),
      'weight' => (int)$row->weight,
    );
  }
  return $out;
}

function xrnote_save_anchor($parent_nid, $note_nid, $uuid, array $selector) {
  db_merge('xrnote_anchor')->key(array('uuid'=>$uuid))
    ->fields(array(
      'parent_nid'=>$parent_nid,
      'note_nid'=>$note_nid,
      'selector'=> json_encode($selector),
      'uid'=> (int) $GLOBALS['user']->uid,
      'changed'=> REQUEST_TIME,
    ))
    ->execute();
}

function _xrnote_enrich_selector($body, array $sel) {
  if (!isset($sel['pos']['start']) || !isset($sel['pos']['end'])) return $sel;
  $start = (int) $sel['pos']['start'];
  $end   = (int) $sel['pos']['end'];
  $win   = XRN_CTX_WIN;

  // Prefix/suffix windows.
  $preLen = min($win, $start);
  $sel['quote']['prefix'] = substr($body, $start - $preLen, $preLen);
  $sel['quote']['suffix'] = substr($body, $end, $win);

  // Context hashes (left/right windows).
  list($cL, $cR) = _xrnote_ctx_hash_pair($body, $start, $end, $win);
  $sel['ctx'] = array('L'=>$cL, 'R'=>$cR, 'win'=>$win);

  // Coarse line hint (use substr() to avoid PHP version args on substr_count()).
  $sel['hint'] = array(
    'line' => substr_count(substr($body, 0, $start), "\n") + 1,
  );
  return $sel;
}