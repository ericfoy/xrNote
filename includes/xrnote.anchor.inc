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
  // Replace 'field_main_text' with your actual body field machine name.
  $fn = 'field_main_text';
  if (empty($node->{$fn})) return NULL;
  $lang = field_language('node',$node,$fn);
  return isset($node->{$fn}[$lang][0]['value']) ? $node->{$fn}[$lang][0]['value'] : NULL;
}

// Stubs for mapping and quote matching.
function _xrnote_map_offsets($patches, $start, $end) { /* implement via patch_apply + index shifts */ return FALSE; }
function _xrnote_find_quote($text, $q) {
  // $q = array('exact'=>'...', 'prefix'=>'...', 'suffix'=>'...')
  if (empty($q['exact'])) return FALSE;
  $pos = strpos($text, $q['exact']);
  if ($pos === FALSE) return FALSE;
  return array($pos, $pos + strlen($q['exact']));
}
