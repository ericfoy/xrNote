<?php

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

function xrnote_reanchor_node($node) {
  $old = _xrnote_prev_body($node->nid);
  $new = _xrnote_current_body($node);
  if ($old === NULL || $new === NULL || $old === $new) return;

  require_once dirname(__FILE__).'/../lib/diff_match_patch.php';
  $dmp = new diff_match_patch();
  $patches = $dmp->patch_make($old, $new);

  $anchors = xrnote_load_anchors($node->nid);
  foreach ($anchors as $a) {
    $sel = $a['selector'];
    $pos = isset($sel['pos']) ? $sel['pos'] : NULL;
    $quote = isset($sel['quote']) ? $sel['quote'] : NULL;

    $mapped = $pos ? _xrnote_map_offsets($patches, $pos['start'], $pos['end']) : FALSE;
    if ($mapped) {
      $sel['pos'] = array('start'=>$mapped[0],'end'=>$mapped[1]);
    }
    else if ($quote && ($found = _xrnote_find_quote($new, $quote))) {
      $sel['pos'] = array('start'=>$found[0],'end'=>$found[1]);
    }
    else {
      watchdog('xrnote', 'Anchor %uuid unresolved on node %nid.', array('%uuid'=>$a['uuid'],'%nid'=>$node->nid), WATCHDOG_WARNING);
      continue;
    }
    db_update('xrnote_anchor')
      ->fields(array('selector'=>json_encode($sel),'changed'=>REQUEST_TIME))
      ->condition('aid', $a['aid'])
      ->execute();
  }
}

function _xrnote_prev_body($nid) {
  $rev = db_query_range("SELECT nr.vid FROM {node_revision} nr WHERE nr.nid=:n ORDER BY nr.vid DESC", 1, 1, array(':n'=>$nid))->fetchField();
  if (!$rev) return NULL;
  $n = node_load($nid, $rev);
  return _xrnote_current_body($n);
}

function _xrnote_current_body($node) {
  if (!xrnote_is_document_node($node)) return NULL;
  $fn = 'body';

  $lang = function_exists('field_language')
    ? field_language('node', $node, $fn)
    : (defined('LANGUAGE_NONE') ? LANGUAGE_NONE : 'und');
  if (!$lang) $lang = defined('LANGUAGE_NONE') ? LANGUAGE_NONE : 'und';

  if (!empty($node->{$fn}[$lang][0]['safe_value'])) return $node->{$fn}[$lang][0]['safe_value'];
  if (!empty($node->{$fn}[$lang][0]['value']))      return $node->{$fn}[$lang][0]['value'];
  return NULL;
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
