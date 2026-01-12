(function ($, Backdrop, win) {
  // Use this version normally
  /*
  function openDialog(url, title) {
    $.get(url).done(function (html) {
      var $d = $('<div class="xrnote-dialog"></div>').append(html).appendTo('body');
      Backdrop.attachBehaviors($d[0]);
      $d.dialog({ title: title || 'Insert XRNote', modal: true, width: 520, close: function(){ $d.remove(); } });
    });
  }
  */

  // Use this version with error handling
  function openDialog(url, title) {
    if (typeof $.fn.dialog !== 'function') {
      alert('XRNote: jQuery UI dialog is not loaded (missing system/ui.dialog).');
      return;
    }

    $.get(url)
      .done(function (html) {
        var $d = $('<div class="xrnote-dialog"></div>').append(html).appendTo('body');
        // Important: wire up #ajax behaviors inside the newly-added markup.
        if (Backdrop && Backdrop.attachBehaviors) {
          Backdrop.attachBehaviors($d[0]);
        }
        $d.dialog({
          title: title || 'Insert XRNote',
          modal: true,
          width: 520,
          close: function(){ $d.remove(); }
        });
      })
      .fail(function (xhr) {
        alert('XRNote modal GET failed: HTTP ' + xhr.status + '\n' + url);
        if (win.console) console.error('XRNote modal GET failed', { url: url, xhr: xhr });
      });
  }

  function register() {
    if (!win.tinymce || !win.tinymce.PluginManager) {
      return setTimeout(register, 200);
    }

    tinymce.PluginManager.add('xrnote', function (editor) {

      editor.ui.registry.addButton('xrnote', {
        text: 'XRNote',
        onAction: function () {

          // SMOKE TEST: prove the click handler runs.
          editor.insertContent('@');
          if (win.console) console.log('XRNote button clicked');
          // NOTE: Do NOT return here, or the dialog/event flow will never run.
          // return;

          var sel = editor.selection, rng = sel.getRng();
          var exact   = sel.getContent({ format: 'text' }) || '';
          var textAll = editor.getContent({ format: 'text' }) || '';
          var start   = rng.startOffset || 0;
          var end     = rng.endOffset || start;
          var prefix  = textAll.substr(Math.max(start - 20, 0), 20);
          var suffix  = textAll.substr(end, 20);

          var uuid = 'xr-' + Date.now() + '-' + Math.floor(Math.random() * 1e6);

          // Prefer Backdrop.settings from the top window (TinyMCE can be iframed).
          var st = (win.top && win.top.Backdrop && win.top.Backdrop.settings && win.top.Backdrop.settings.xrnote)
            ? win.top.Backdrop.settings.xrnote
            : (Backdrop.settings.xrnote || {});

          // Resolve basePath robustly.
          var basePath =
            (st && typeof st.basePath === 'string') ? st.basePath :
            (win.top && win.top.Backdrop && win.top.Backdrop.settings && win.top.Backdrop.settings.basePath) ? win.top.Backdrop.settings.basePath :
            (Backdrop.settings && Backdrop.settings.basePath) ? Backdrop.settings.basePath :
            '/';

          var nid = st && st.nid ? st.nid : null;
          if (!nid) {
            alert('XRNote: missing Backdrop.settings.xrnote.nid');
            return;
          }

          var url = basePath + 'xrnote/modal/add/' + nid + '/' + uuid;
          if (win.console) console.log('XRNote settings/url', { st: st, url: url });

          // Listen for the insert event on the TOP document, not the TinyMCE iframe.
          var $top = (win.top && win.top.jQuery) ? win.top.jQuery : $;
          var $doc = $top(win.top ? win.top.document : document);

          // Optional debug: logs whenever any xrnote-insert is seen in the top doc.
          // Comment out once working.
          $doc.off('xrnote-insert._xrdebug').on('xrnote-insert._xrdebug', function (e, payload) {
            if (win.top && win.top.console) win.top.console.log('[xrnote DEBUG] event seen:', payload);
          });

          function onInsert(e, payload) {
            try {
              var d = payload || {};
              if (win.top && win.top.console) win.top.console.log('[xrnote] onInsert fired', d);

              if (d.uuid !== uuid || !d.note_nid) return;

              var marker =
                '<span class="xrnote-anchor" data-xr-uuid="' + uuid +
                '" data-note-nid="' + d.note_nid + '">[XR]</span>';

              editor.insertContent(marker);

              // Persist the anchor (async; ok if it finishes after dialog close).
              $top.post
              $top
              $.post(basePath + 'xrnote/anchors/' + nid, {
                op: 'save',
                uuid: uuid,
                note_nid: d.note_nid,
                selector: JSON.stringify({
                  pos: { start: start, end: end },
                  quote: { exact: exact, prefix: prefix, suffix: suffix }
                })
              });

              // Close the dialog we opened (jQuery UI dialog).
              if ($top.fn && $top.fn.dialog) {
                $top('.xrnote-dialog').dialog('close');
              }

              // Unbind one-shot handler.
              $doc.off('xrnote-insert.' + uuid, onInsert);
            }
            catch (err) {
              if (win.top && win.top.console) win.top.console.error('[xrnote] onInsert error', err);
              alert('XRNote onInsert error: ' + (err && err.message ? err.message : err));
            }
          }

          // Bind one-shot handler namespaced by uuid.
          $doc.off('xrnote-insert.' + uuid, onInsert).on('xrnote-insert.' + uuid, onInsert);

          // Open the modal.
          openDialog(url, 'Insert XRNote');
        }
      });

      return {};
    });
  }
  register();

})(jQuery, Backdrop, window);
