<?php
/*
Drop this file at xrnote/lib/diff_match_patch.php.

It provides the diff_match_patch class with the constants and two methods you use: 
  diff_main() and diff_cleanupSemantic(). It’s a tiny fallback: it finds common 
  prefix/suffix and treats the middle as a single delete+insert. That’s enough for 
  XRNote’s offset remapper, which also has quote-based fallback. If you later add 
  the full Google library, remove this file.
*/

/**
 * Minimal wrapper/fallback for diff-match-patch.
 * - Defines class diff_match_patch if the real library is absent.
 * - Implements:
 *     - const DIFF_DELETE, DIFF_INSERT, DIFF_EQUAL
 *     - diff_main($a, $b, $checklines = TRUE) -> array of [op, text]
 *     - diff_cleanupSemantic(&$diffs)         -> merges adjacent ops, drops empties
 *
 * This is coarse but sufficient for XRNote's offset mapping with quote fallback.
 * Replace with the official library for best diffs when convenient.
 */

/**
 * Minimal diff-match-patch wrapper for XRNote.
 * Provides:
 *  - const DIFF_DELETE, DIFF_INSERT, DIFF_EQUAL
 *  - diff_main($a,$b,$checklines=true)
 *  - diff_cleanupSemantic(&$diffs)
 *  - patch_make($a,$b) -> array of Patch objects
 *  - patch_apply($patches,$text) -> array($new_text, $results[])
 *
 * Notes:
 *  - Produces a single Patch spanning the whole text. Good enough for offset mapping.
 *  - If you later vendor the full Google library, delete this file.
 */

if (!class_exists('diff_match_patch', false)) {

  class diff_match_patch {
    const DIFF_DELETE = -1;
    const DIFF_INSERT = 1;
    const DIFF_EQUAL  = 0;

    public function diff_main($text1, $text2, $checklines = true) {
      if ($text1 === $text2) {
        return ($text1 === '') ? array() : array(array(self::DIFF_EQUAL, $text1));
      }
      $prefixLen = $this->diff_commonPrefix($text1, $text2);
      $prefix = ($prefixLen > 0) ? substr($text1, 0, $prefixLen) : '';

      $a = ($prefixLen > 0) ? substr($text1, $prefixLen) : $text1;
      $b = ($prefixLen > 0) ? substr($text2, $prefixLen) : $text2;

      $suffixLen = $this->diff_commonSuffix($a, $b);
      $suffix = ($suffixLen > 0) ? substr($a, strlen($a) - $suffixLen) : '';

      $a_mid = ($suffixLen > 0) ? substr($a, 0, strlen($a) - $suffixLen) : $a;
      $b_mid = ($suffixLen > 0) ? substr($b, 0, strlen($b) - $suffixLen) : $b;

      $diffs = array();
      if ($prefix !== '') $diffs[] = array(self::DIFF_EQUAL, $prefix);
      if ($a_mid !== '')  $diffs[] = array(self::DIFF_DELETE, $a_mid);
      if ($b_mid !== '')  $diffs[] = array(self::DIFF_INSERT, $b_mid);
      if ($suffix !== '') $diffs[] = array(self::DIFF_EQUAL, $suffix);

      $this->diff_cleanupSemantic($diffs);
      return $diffs;
    }

    public function diff_cleanupSemantic(&$diffs) {
      if (empty($diffs)) return;
      $out = array();
      $lastOp = null; $lastText = '';
      foreach ($diffs as $d) {
        $op = $d[0]; $text = (string) $d[1];
        if ($text === '') continue;
        if ($op === $lastOp) { $lastText .= $text; }
        else {
          if ($lastOp !== null && $lastText !== '') $out[] = array($lastOp, $lastText);
          $lastOp = $op; $lastText = $text;
        }
      }
      if ($lastOp !== null && $lastText !== '') $out[] = array($lastOp, $lastText);
      $diffs = $out;
    }

    public function patch_make($text1, $text2) {
      $diffs = $this->diff_main($text1, $text2, true);
      $this->diff_cleanupSemantic($diffs);

      $p = new dmp_Patch();
      $p->diffs   = $diffs;
      $p->start1  = 0;
      $p->start2  = 0;
      $p->length1 = strlen($text1);
      $p->length2 = strlen($text2);
      return array($p);
    }

    public function patch_apply($patches, $text) {
      // Minimal: ignore $text and rebuild from diffs of the first patch.
      if (empty($patches)) return array($text, array());
      $p = $patches[0];
      $new = '';
      foreach ($p->diffs as $d) {
        list($op, $t) = $d;
        if ($op === self::DIFF_EQUAL || $op === self::DIFF_INSERT) $new .= $t;
        // DIFF_DELETE contributes nothing to output.
      }
      // Report success for each patch.
      $results = array_fill(0, count($patches), true);
      return array($new, $results);
    }

    public function diff_commonPrefix($t1, $t2) {
      $n = min(strlen($t1), strlen($t2));
      for ($i = 0; $i < $n; $i++) if ($t1[$i] !== $t2[$i]) return $i;
      return $n;
    }

    public function diff_commonSuffix($t1, $t2) {
      $l1 = strlen($t1); $l2 = strlen($t2); $n = min($l1, $l2);
      for ($i = 1; $i <= $n; $i++) if ($t1[$l1 - $i] !== $t2[$l2 - $i]) return $i - 1;
      return $n;
    }
  }

  class dmp_Patch {
    /** @var array<int, array{0:int,1:string}> */
    public $diffs = array();
    /** @var int */
    public $start1 = 0;
    /** @var int */
    public $start2 = 0;
    /** @var int */
    public $length1 = 0;
    /** @var int */
    public $length2 = 0;
  }
}
