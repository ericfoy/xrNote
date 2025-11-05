<?php

/**
 * Text filter example: replace anchor spans with inline <aside>.
 * Provide a real filter via hook_filter_info() if you want administrators to toggle.
 */
function xrnote_inline_replace($html) {
  return preg_replace_callback('#<span class="xrnote-anchor"[^>]*data-note-nid="(\d+)"[^>]*></span>#',
    function ($m) {
      $nid = (int) $m[1];
      $note = node_load($nid);
      if (!$note) return '';
      $lang = field_language('node',$note,'field_xr_body');
      $body = isset($note->field_xr_body[$lang][0]['value']) ? $note->field_xr_body[$lang][0]['value'] : '';
      return '<aside class="xrnote-inline">'.$body.'</aside>';
    },
    $html
  );
}
