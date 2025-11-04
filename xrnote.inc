<?php

/**
 * Parse a multiline directives string into structured directives.
 *
 * One directive per line:
 *   {target} [pre] [{include}] [post][_]
 *
 * - {target}: literal. May contain braces if wrapped in quotes:
 *     {"this {string}"} or {'this {string}'}
 * - pre/post: N[c|w], where "c"=characters, "w"=words. "0" allowed. If the
 *   unit letter is omitted, "c" is assumed.
 * - {include}: text inserted in place of the target. Use '@' to inject the
 *   original target. Example: {@ was found.}
 * - Trailing "_" on the line makes the join between this and the next snippet
 *   a single space instead of a newline.
 *
 * If {include} is omitted, '{@}' is assumed.
 */
function scan_text_parse_directives($text) {
  $result = [];
  $lines = preg_split('/\R/u', (string) $text, -1);
  foreach ($lines as $raw_line) {
    $line = $raw_line; // preserve as typed
    $soft_join = false;

    // Trailing "_" ⇒ join with space instead of CRLF.
    if ($line !== '' && substr($line, -1) === '_') {
      $soft_join = true;
      $line = rtrim(substr($line, 0, -1), " \t");
    }

    // -------- Pre-parse: modal redirection lines ("\>", "\>>") ----------
    if ($line !== '' && $line[0] === '\\') {
      // Modal: \>>[node:x], \>[node:x], or \> to cancel.
      if (isset($line[1]) && $line[1] === '>') {
        if (preg_match('/^\\\\(>>?)\s*(\[[^\]]+\])?\s*$/u', $line, $mm)) {
          $op = $mm[1];
          $dst = NULL;
          if (!empty($mm[2])) {
            $dst = _scan_text_parse_dest($mm[2]); // NULL means cancel
          }
          $result[] = [
            'mode'      => 'redir_mode',
            'exclusive' => ($op === '>>'),
            'dest'      => $dst, // NULL => cancel
          ];
          continue;
        }
      }

      // ---------------- Exclude / comment handling (unchanged) ----------------
      if (isset($line[1]) && $line[1] === '-') {
        $rem = ltrim(substr($line, 2));
        if ($rem !== '') {
          // "\..": exclude between START and END (supports \n on each part).
          if (preg_match('/\\\\\.\./', $rem)) {
            $parts = preg_split('/\\\\\.\./', $rem, 2);
            $pa = _scan_text_parse_anchors(trim($parts[0]));
            $pb = _scan_text_parse_anchors(trim($parts[1] ?? ''));
            if ($pa['text'] !== '' && $pb['text'] !== '') {
              $result[] = [
                'mode' => 'exclude_between',
                // start term + anchors
                'start' => $pa['text'],
                'start_a_start' => $pa['start'],
                'start_a_end'   => $pa['end'],
                // end term + anchors
                'end' => $pb['text'],
                'end_a_start' => $pb['start'],
                'end_a_end'   => $pb['end'],
              ];
            }
          }
          // Trailing backtick/backslash => exclude any non-space token starting with prefix.
          elseif (in_array(substr($rem, -1), ['`', '\\'], true)) {
            $prefix = rtrim(substr($rem, 0, -1));
            if ($prefix !== '') {
              $result[] = ['mode' => 'exclude_prefix', 'prefix' => $prefix];
            }
          }
          // Literal global exclude (supports \n at start/end).
          else {
            $pa = _scan_text_parse_anchors($rem);
            if ($pa['text'] !== '') {
              $result[] = [
                'mode' => 'exclude',
                'text' => $pa['text'],
                'a_start' => $pa['start'],
                'a_end'   => $pa['end'],
              ];
            }
          }
        }
      }
      // Comment line otherwise: ignore.
      continue;
    }

    // Blank literal line (allowed; re-emit)
    if ($line === '') {
      $result[] = ['mode' => 'literal', 'text' => '', 'soft_join' => $soft_join];
      continue;
    }

    // -------- Pre-parse: single-shot redirection suffix (" … >[node:x]") ----------
    $shot = _scan_text_strip_redir_suffix($line);
    $line = $shot['line'];
    $redir_once = $shot['once']; // ['dest'=>..., 'exclusive'=>bool] | NULL

    // Directive line if it begins (optionally after spaces) with "{"
    if (preg_match('/^\s*\{/', $line)) {
      $i = 0;
      while ($i < strlen($line) && ctype_space($line[$i])) $i++;

      list($target, $i) = _scan_text_read_braced($line, $i);
      if ($target === null) throw new \InvalidArgumentException("Directive needs a {target}: {$raw_line}");

      while ($i < strlen($line) && ctype_space($line[$i])) $i++;
      list($pre_len, $i) = _scan_text_read_len_token($line, $i);
      if ($pre_len === null) $pre_len = ['n' => 0, 'u' => 'c'];

      while ($i < strlen($line) && ctype_space($line[$i])) $i++;
      $include = '{@}';
      if ($i < strlen($line) && $line[$i] === '{') {
        list($include, $i) = _scan_text_read_braced($line, $i);
        if ($include === null) throw new \InvalidArgumentException("Malformed {include}: {$raw_line}");
        while ($i < strlen($line) && ctype_space($line[$i])) $i++;
      }

      // NOTE: uses the post-token reader that supports +STOP / -STOP (already added)
      list($post_len, $i) = _scan_text_read_post_token($line, $i);
      if ($post_len === null) $post_len = ['n' => 0, 'u' => 'c'];

      $result[] = [
        'mode'      => 'directive',
        'target'    => $target,
        'pre'       => $pre_len,
        'include'   => $include,
        'post'      => $post_len,
        'soft_join' => $soft_join,
        // one-shot redirection flags for this line only
        'redir_once' => $redir_once, // NULL or ['dest'=>..,'exclusive'=>bool]
      ];
    }
    else {
      // Plain literal line
      $result[] = [
        'mode'      => 'literal',
        'text'      => $line,
        'soft_join' => $soft_join,
        'redir_once'=> $redir_once, // NULL or ['dest'=>..,'exclusive'=>bool]
      ];
    }
  }
  return $result;
}

/**
 * Apply directives to a document string. Returns the composed summary.
 * By default finds the first occurrence of each target. Case-sensitive.
 */
/**
 * Apply directives with simple redirection.
 * Returns ['text' => string, 'appends' => ['node:FIELD' => "...", ...]].
 */
function scan_text_apply_directives($document, $directives_text, $ctx = array()) {
  $directives = is_array($directives_text)
    ? $directives_text
    : scan_text_parse_directives($directives_text);

  // Collect exclude rules to apply after composition (same buffers).
  $ex_literals = array();
  $ex_pairs    = array();
  $ex_prefixes = array();

  $stream = array();
  foreach ($directives as $d) {
    if (!empty($d['mode'])) {
      if ($d['mode'] === 'exclude' && !empty($d['text']))            { $ex_literals[] = $d; continue; }
      if ($d['mode'] === 'exclude_between' && !empty($d['start']) &&
          !empty($d['end']))                                         { $ex_pairs[]    = $d; continue; }
      if ($d['mode'] === 'exclude_prefix' && !empty($d['prefix']))   { $ex_prefixes[] = $d['prefix']; continue; }
      // Keep redir_mode and emittables in the stream.
    }
    $stream[] = $d;
  }

  // Modal state.
  $mode_dest = NULL;       // 'node:FIELD'
  $mode_excl = false;      // exclusive modal redirection?
  $mode_incl = false;      // inclusive modal redirection?

  $out = '';
  $appends = array();

  $append_to = function($dest_key, $text) use (&$appends) {
    if ($text === '') return;
    if (!isset($appends[$dest_key])) $appends[$dest_key] = '';
    $appends[$dest_key] .= $text;
  };

  $n = count($stream);
  foreach ($stream as $idx => $d) {
    // Handle modal mode set/cancel.
    if (!empty($d['mode']) && $d['mode'] === 'redir_mode') {
      if (!empty($d['dest'])) {
        $mode_dest = 'node:' . $d['dest']['field'];
        $mode_excl = !empty($d['exclusive']);
        $mode_incl = empty($d['exclusive']); // '>' means inclusive
      } else {
        // Cancel any modal redirection.
        $mode_dest = NULL;
        $mode_excl = $mode_incl = false;
      }
      continue;
    }

    // Build snippet.
    $snippet = (!empty($d['mode']) && $d['mode'] === 'literal')
      ? $d['text']
      : scan_text_execute_one($document, $d);

    // Join rule for this entry.
    $is_last = ($idx === $n - 1);
    $join = (!empty($d['soft_join']) && !$is_last) ? ' ' : (!$is_last ? "\r\n" : '');

    // One-shot flags.
    $once_dest = NULL;
    $once_excl = false;
    $once_incl = false;
    if (!empty($d['redir_once']['dest'])) {
      $once_dest = 'node:' . $d['redir_once']['dest']['field'];
      $once_excl = !empty($d['redir_once']['exclusive']);
      $once_incl = empty($d['redir_once']['exclusive']);
    }

    // Decide destination for this line.
    $dest = $once_dest ?: $mode_dest;                       // prefer one-shot over modal
    $send_to_dest = (bool)($dest && ($once_excl || $once_incl || $mode_excl || $mode_incl));
    $exclusive = (bool)($once_excl || (!$once_dest && $mode_excl)); // one-shot overrides modal

    if ($snippet !== '') {
      if ($send_to_dest) $append_to($dest, $snippet . $join);
      if (!$exclusive)   $out .= $snippet . $join;
    }
    // One-shot flags auto-clear by design since they’re only on this $d.
  }

  // Optional: apply your global excludes to the composed buffers (if you use them).
  if (function_exists('_scan_text_apply_global_excludes')) {
    if ($out !== '') $out = _scan_text_apply_global_excludes($out, $ex_literals, $ex_pairs, $ex_prefixes);
    if (!empty($appends)) {
      foreach ($appends as $k => $buf) {
        $appends[$k] = _scan_text_apply_global_excludes($buf, $ex_literals, $ex_pairs, $ex_prefixes);
      }
    }
  }

  return array('text' => $out, 'appends' => $appends);
}

/**
 * Execute one directive and return its snippet.
 */

function scan_text_execute_one($document, array $d) {
  // Support \n anchors in target: {\nX} start-of-line, {X\n} end-of-line, {\nX\n} whole line.
  $raw_target   = $d['target'];
  $anchor_start = (substr($raw_target, 0, 2) === '\\n');
  $anchor_end   = (substr($raw_target, -2) === '\\n');
  if ($anchor_start) $raw_target = substr($raw_target, 2);
  if ($anchor_end)   $raw_target = substr($raw_target, 0, -2);
  $target = $raw_target;

  // Locate target, with optional line anchors.
  if ($anchor_start || $anchor_end) {
    $re_t = preg_quote($target, '/');
    if ($anchor_start && $anchor_end) {
      $pat = '/^\h*(' . $re_t . ')\h*$/um';
    }
    elseif ($anchor_start) {
      $pat = '/^\h*(' . $re_t . ')/um';
    }
    else { // $anchor_end
      $pat = '/(' . $re_t . ')\h*$/um';
    }
    if (!preg_match($pat, $document, $m, PREG_OFFSET_CAPTURE)) return '';
    $pos  = $m[1][1];
    $tlen = strlen($m[1][0]);
  }
  else {
    $pos = strpos($document, $target);
    if ($pos === false) return '';
    $tlen = strlen($target);
  }

  $before = substr($document, 0, $pos);
  $after  = substr($document, $pos + $tlen);

  // Apply optional stop limiter on the post side.
  $after_limited = $after;
  if (!empty($d['post']['stop_text'])) {
    $sp = strpos($after, $d['post']['stop_text']);
    if ($sp !== false) {
      $limit = $sp + (!empty($d['post']['stop_include']) ? strlen($d['post']['stop_text']) : 0);
      $after_limited = substr($after, 0, $limit);
    }
  }

  $pre  = _scan_text_take($before, $d['pre'],  true);
  $post = _scan_text_take($after_limited,  $d['post'], false);

  $include = str_replace('@', $target, $d['include']);

  // Preserve internal newlines. Trim only edge CR/LF.
  $pre  = rtrim($pre,  "\r\n");
  $post = rtrim($post, "\r\n");
  // Drop only horizontal space at start of post (keep leading \n if present).
  $post = preg_replace('/^\h+/u', '', $post);

  // Assemble without collapsing embedded newlines.
  $snippet = '';
  if ($pre !== '') {
    $snippet .= rtrim($pre, " \t");
  }
  if ($include !== '') {
    if ($snippet !== '' && !preg_match('/\s$/u', $snippet)) $snippet .= ' ';
    $snippet .= trim($include);
  }
  if ($post !== '') {
    if ($snippet !== '' && !preg_match('/\s$/u', $snippet) && !preg_match('/^\s/u', $post)) {
      $snippet .= ' ';
    }
    $snippet .= $post;
  }

  // Normalize spaces only (not newlines).
  $snippet = preg_replace('/[ \t]+/u', ' ', $snippet);
  return $snippet;
}

/* ========================== helpers ========================== */

/**
 * Read a { ... } block starting at $i. Supports quoted inner text that may
 * contain braces: {"a {b} c"} or {'a {b} c'}.
 * Returns [string|null, new_index].
 */
function _scan_text_read_braced($line, $i) {
  $len = strlen($line);
  while ($i < $len && ctype_space($line[$i])) $i++;
  if ($i >= $len || $line[$i] !== '{') return [null, $i];
  $i++; // skip '{'

  $buf = '';
  $in_quote = false;
  $quote_ch = null;

  for (; $i < $len; $i++) {
    $ch = $line[$i];

    if ($in_quote) {
      if ($ch === $quote_ch) {
        $in_quote = false;
        $quote_ch = null;
        continue;
      }
      $buf .= $ch;
      continue;
    }

    if ($ch === '"' || $ch === "'") {
      $in_quote = true;
      $quote_ch = $ch;
      continue;
    }

    if ($ch === '}') {
      // End of block.
      return [$buf, $i + 1];
    }

    $buf .= $ch;
  }

  // Unterminated.
  return [null, $i];
}

/**
 * Read an optional length token at $i:  N[w|c]?  or "0"
 * Returns [[ 'n'=>int, 'u'=>'w'|'c' ]|null, new_index].
 */
function _scan_text_read_len_token($line, $i) {
  $len = strlen($line);
  $start = $i;
  // Skip leading spaces.
  while ($i < $len && ctype_space($line[$i])) $i++;

  // Read digits.
  $j = $i;
  while ($j < $len && ctype_digit($line[$j])) $j++;

  if ($j === $i) {
    // No digits => no token.
    return [null, $start];
  }

  $num = (int) substr($line, $i, $j - $i);
  $unit = 'c';

  // Optional unit letter.
  if ($j < $len && ($line[$j] === 'w' || $line[$j] === 'c' || $line[$j] === 'W' || $line[$j] === 'C')) {
    $unit = strtolower($line[$j]);
    $j++;
  }

  return [[ 'n' => $num, 'u' => $unit ], $j];
}

/**
 * Take N words or characters from the left (post=false) or right (pre=true)
 * side of a string. Word mode counts non-space runs as "words" and preserves
 * attached punctuation and the original inter-word spacing.
 */
function _scan_text_take($s, array $len_spec, $from_right) {
  $n = (int) $len_spec['n'];
  $u = $len_spec['u']; // 'w' or 'c'
  if ($n <= 0) return '';

  if ($u === 'c') {
    if ($from_right) {
      $slice = substr($s, max(0, strlen($s) - $n));
      return $slice;
    }
    else {
      return substr($s, 0, $n);
    }
  }

  // Word mode: tokenize as non-space + trailing spaces.
  preg_match_all('/\S+\s*/u', $s, $m);
  $tokens = $m[0];

  if ($from_right) {
    if ($n >= count($tokens)) return $s;
    $tokens = array_slice($tokens, -$n);
    return implode('', $tokens);
  }
  else {
    if ($n >= count($tokens)) return $s;
    $tokens = array_slice($tokens, 0, $n);
    return implode('', $tokens);
  }
}

// NEW
function _scan_text_read_post_token($line, $i) {
  list($len, $i2) = _scan_text_read_len_token($line, $i);
  if ($len === null) return [null, $i];

  $L = strlen($line);
  // Skip spaces after Nw/Nc.
  while ($i2 < $L && ctype_space($line[$i2])) $i2++;

  // Optional +STOP or -STOP (plain text until end of line).
  if ($i2 < $L && ($line[$i2] === '+' || $line[$i2] === '-')) {
    $include = ($line[$i2] === '+');
    $i2++;
    // Consume one optional space after +/-
    if ($i2 < $L && $line[$i2] === ' ') $i2++;
    $stop = rtrim(substr($line, $i2)); // rest of line
    if ($stop !== '') {
      $len['stop_text'] = $stop;
      $len['stop_include'] = $include;
      // We consumed the line.
      return [$len, $L];
    }
  }

  return [$len, $i2];
}

function _scan_text_parse_anchors($s) {
  $a = ['start' => false, 'end' => false, 'text' => $s];
  if (substr($a['text'], 0, 2) === '\\n') { $a['start'] = true; $a['text'] = substr($a['text'], 2); }
  if (substr($a['text'], -2) === '\\n')   { $a['end']   = true; $a['text'] = substr($a['text'], 0, -2); }
  return $a;
}

// [node:field_name] → ['type'=>'node','field'=>'field_name'] or NULL.
function _scan_text_parse_dest($bracketed) {
  if (!preg_match('/^\s*\[\s*node:([a-z0-9_]+)\s*\]\s*$/i', $bracketed, $m)) return NULL;
  return ['type' => 'node', 'field' => $m[1]];
}

// Strip a trailing single-shot redirection suffix:  " … >[node:x]" or " … >>[node:x]".
function _scan_text_strip_redir_suffix($line) {
  // Returns ['line'=>string, 'once'=>['dest'=>..,'exclusive'=>bool]|NULL]
  if (preg_match('/^(.*?)[ \t](>>?)\s*(\[[^\]]+\])\s*$/u', $line, $m)) {
    $base = rtrim($m[1]);
    $op   = $m[2];
    $dst  = _scan_text_parse_dest($m[3]);
    if ($dst) {
      return ['line' => $base, 'once' => ['dest' => $dst, 'exclusive' => ($op === '>>')]];
    }
  }
  return ['line' => $line, 'once' => NULL];
}

/**
 * Apply global excludes to a text buffer.
 * @param string $text
 * @param array  $ex_literals  // each: ['text'=>..., 'a_start'=>bool, 'a_end'=>bool]
 * @param array  $ex_pairs     // each: ['start'=>..., 'end'=>..., 'start_a_start'=>bool, 'start_a_end'=>bool, 'end_a_start'=>bool, 'end_a_end'=>bool]
 * @param array  $ex_prefixes  // list of string prefixes
 * @return string
 */
function _scan_text_apply_global_excludes($text, array $ex_literals, array $ex_pairs, array $ex_prefixes) {
  // -------- 1) Literal excludes --------
  foreach ($ex_literals as $lit) {
    if (empty($lit['text'])) continue;
    $re = preg_quote($lit['text'], '/');

    if (!empty($lit['a_start']) && !empty($lit['a_end'])) {
      // Whole-line match: remove the entire line (and its newline or EOF).
      $text = preg_replace('/^\h*' . $re . '\h*(?:\r?\n|$)/um', '', $text);
      continue;
    }

    if (!empty($lit['a_start'])) {
      // Prefer removal when it reaches EOL, else just the anchored text.
      $text = preg_replace('/^\h*' . $re . '\h*(?:\r?\n|$)/um', '', $text);
      $text = preg_replace('/^\h*' . $re . '/um', '', $text);
      continue;
    }

    if (!empty($lit['a_end'])) {
      // Prefer removal at EOL (eat newline), else just the text.
      $text = preg_replace('/' . $re . '\h*$(?:\r?\n)?/um', '', $text);
      $text = preg_replace('/' . $re . '/um', '', $text);
      continue;
    }

    // Unanchored: first eat if at EOL, then anywhere else.
    $text = preg_replace('/' . $re . '\h*(?:\r?\n|$)/u', '', $text);
    $text = preg_replace('/' . $re . '/u', '', $text);
  }

  // -------- 2) Start…end span excludes --------
  foreach ($ex_pairs as $p) {
    if (empty($p['start']) || empty($p['end'])) continue;

    $sa = preg_quote($p['start'], '/');
    $ea = preg_quote($p['end'],   '/');

    // Build anchored fragments.
    if (!empty($p['start_a_start']) && !empty($p['start_a_end']))      $start_pat = '^\h*' . $sa . '\h*$';
    elseif (!empty($p['start_a_start']))                                $start_pat = '^\h*' . $sa;
    elseif (!empty($p['start_a_end']))                                  $start_pat = $sa . '\h*$';
    else                                                                $start_pat = $sa;

    if (!empty($p['end_a_start']) && !empty($p['end_a_end']))          $end_pat = '^\h*' . $ea . '\h*$';
    elseif (!empty($p['end_a_start']))                                  $end_pat = '^\h*' . $ea;
    elseif (!empty($p['end_a_end']))                                    $end_pat = $ea . '\h*$';
    else                                                                $end_pat = $ea;

    // Remove span non-greedily across lines; trim one trailing newline if span hits EOL.
    $text = preg_replace('/' . $start_pat . '(?:[\s\S]*?)' . $end_pat . '(?:\r?\n)?/um', '', $text);
  }

  // -------- 3) Prefix excludes (whitespace is the only word break) --------
  foreach ($ex_prefixes as $pre) {
    if ($pre === '') continue;
    $text = preg_replace('/(^|\s)' . preg_quote($pre, '/') . '\S*/u', '$1', $text);
  }

  return $text;
}

/**
 * Append redirected text into node fields.
 * Dest keys must be "node:FIELD_NAME".
 * Works with text/text_long/text_with_summary (appends to ['value']).
 */
function _scan_text_apply_redirects_to_node($node, $langcode, array $appends) {
  foreach ($appends as $dest_key => $append_text) {
    if (strpos($dest_key, 'node:') !== 0) continue;

    $field = substr($dest_key, 5);

    // Special-case: base property "title".
    if ($field === 'title') {
      $node->title = (string) $append_text;
      continue;
    }

    // Validate field exists and is a text type.
    $fi = field_info_field($field);
    if (!$fi) {
      watchdog('scan_text', 'Redirect destination missing field definition: %field', array('%field' => $field), WATCHDOG_WARNING);
      continue;
    }
    if (!in_array($fi['type'], array('text', 'text_long', 'text_with_summary'), true)) {
      watchdog('scan_text', 'Redirect refused: %field is not a text field (type=%type).', array('%field' => $field, '%type' => $fi['type']), WATCHDOG_WARNING);
      continue;
    }

    // Initialize field container on the node if absent.
    if (!isset($node->{$field}) || !is_array($node->{$field})) {
      $node->{$field} = array();
    }

    // Choose language bucket.
    if (isset($node->{$field}[$langcode])) {
      $lc = $langcode;
    }
    elseif (isset($node->{$field}['und'])) {
      $lc = 'und';
    }
    else {
      // Fall back to first existing language or create 'und'.
      $keys = array_keys($node->{$field});
      $lc = $keys ? reset($keys) : 'und';
      if (!isset($node->{$field}[$lc])) {
        $node->{$field}[$lc] = array();
      }
    }

    // Ensure delta 0 exists and has a 'value'.
    if (empty($node->{$field}[$lc][0]) || !is_array($node->{$field}[$lc][0])) {
      $node->{$field}[$lc][0] = array('value' => '');
    }
    if (!array_key_exists('value', $node->{$field}[$lc][0])) {
      $node->{$field}[$lc][0]['value'] = '';
    }

    // Append.
    $node->{$field}[$lc][0]['value'] = (string) $append_text;
  }
}

/* ========================== self-test ========================== */

if (PHP_SAPI === 'cli' && basename(__FILE__) === basename($_SERVER['argv'][0] ?? '')) {
  $doc = "Dictionaries often use a multi-column layout to save space, with running headers indicating keyword entries on that page. Notice how letters are rotated and shown on tabs on the side of right pages. The fonts used in this sample PDF are Satyr and Faunus, made by Monokrom. Archive.org has a scanned copy of the printed edition of this Old Icelandic dictionary from 1910.";

  $defs = <<<TXT
{letters are rotated} 2w {@} 4w
{letters are rotated} 2w {} 4w
{keyword} 0 {} 4w_
{Icelandic} 0 {@ was found in this document.} 0
TXT;

  $out = scan_text_apply_directives($doc, $defs);
  echo $out, "\n";
  /* Expected:
  Notice how letters are rotated and shown on tabs
  Notice how and shown on tabs
  entries on that page. Icelandic was found in this document.
  */
}

/* ====== This is for falling back to read from the body field until we get pdftotext working */

/**
 * Convert HTML-ish body content to plain text.
 */
function scan_text_plainify($html) {
  $t = html_entity_decode(strip_tags($html), ENT_QUOTES, 'UTF-8');
  $t = preg_replace("/\r\n?/", "\n", $t);
  $t = preg_replace("/[ \t]+\n/", "\n", $t);
  $t = preg_replace("/[ \t]{2,}/", " ", $t);
  return trim($t);
}

/**
 * Get body field text as plain text for a node.
 */
function scan_text_body_plaintext($entity, $langcode = NULL) {
  if (empty($entity->body) || !is_array($entity->body)) return '';

  // Preferred order: exact lang, 'und', then any.
  $candidates = array();

  if ($langcode && !empty($entity->body[$langcode][0]['value'])) {
    $candidates[] = $entity->body[$langcode][0]['value'];
  }
  if (!empty($entity->body['und'][0]['value'])) {
    $candidates[] = $entity->body['und'][0]['value'];
  }
  foreach ($entity->body as $lc => $items) {
    if (!empty($items[0]['value'])) {
      $candidates[] = $items[0]['value'];
    }
  }

  foreach ($candidates as $html) {
    $txt = scan_text_plainify($html);
    if ($txt !== '') return $txt;
  }
  return '';
}
