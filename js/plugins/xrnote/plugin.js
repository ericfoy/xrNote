(function ($, Backdrop, win) {

  function register() {
    if (!win.tinymce || !win.tinymce.PluginManager) {
      return setTimeout(register, 200);
    }

    tinymce.PluginManager.add('xrnote', function (editor) {

      editor.ui.registry.addButton('xrnote', {
        text: 'XRNote',
        onAction: function () {

          // SMOKE TEST: prove the click handler runs.
          // editor.insertContent('@');
          if (win.console) console.log('XRNote button clicked');
          // return; // keep commented out

          var sel = editor.selection, rng = sel.getRng();
          var exact   = sel.getContent({ format: 'text' }) || '';
          var textAll = editor.getContent({ format: 'text' }) || '';
          var start   = rng.startOffset || 0;
          var end     = rng.endOffset || start;
          var prefix  = textAll.substr(Math.max(start - 20, 0), 20);
          var suffix  = textAll.substr(end, 20);

          var uuid = 'xr-' + Date.now() + '-' + Math.floor(Math.random() * 1e6);

          // Always prefer TOP window Backdrop + jQuery (TinyMCE may be iframed).
          var BD = (win.top && win.top.Backdrop) ? win.top.Backdrop : Backdrop;
          var $page = (win.top && win.top.jQuery) ? win.top.jQuery : $;

          // Pull settings from the top window if available.
          var st = (BD && BD.settings && BD.settings.xrnote) ? BD.settings.xrnote : (Backdrop.settings.xrnote || {});
          var basePath = (st && typeof st.basePath === 'string') ? st.basePath : (BD && BD.settings && BD.settings.basePath ? BD.settings.basePath : '/');
          var nid = st && st.nid ? st.nid : null;

          if (!nid) {
            alert('XRNote: missing Backdrop.settings.xrnote.nid');
            return;
          }

          var url = basePath + 'xrnote/modal/add/' + nid + '/' + uuid;
          if (win.console) console.log('XRNote settings/url', { st: st, url: url });

          // Listen for the insert event on the TOP document (event bubbles from body).
          var $doc = $page(win.top ? win.top.document : document);

          function onInsert(e, payload) {
            var d = payload || {};
            if (win.top && win.top.console) win.top.console.log('[xrnote] onInsert fired', d);

            // Ignore other inserts.
            if (d.uuid !== uuid || !d.note_nid) return;

            // Unbind immediately (one-shot) to avoid any chance of re-entry.
            $doc.off('xrnote-insert.' + uuid, onInsert);

            // IMPORTANT: defer heavy work until after Backdrop finishes its AJAX cycle.
            setTimeout(function () {
              try {
                var BD = (win.top && win.top.Backdrop) ? win.top.Backdrop : Backdrop;
                var $page = (win.top && win.top.jQuery) ? win.top.jQuery : $;

                // Close the Backdrop dialog that contains our form wrapper (if it’s still open).
                var $dlg = $page('#xrnote-modal-add-wrapper').closest('.ui-dialog-content');
                if ($dlg.length) {
                  if (BD && BD.dialog) {
                    BD.dialog($dlg).close();
                  }
                  else if ($page.fn && $page.fn.dialog) {
                    $dlg.dialog('close');
                  }
                }

                // Insert the marker in TinyMCE.
                var marker =
                  '<span class="xrnote-anchor" data-xr-uuid="' + uuid +
                  '" data-note-nid="' + d.note_nid + '">[XR]</span>';

                editor.insertContent(marker);

                // Persist the anchor (async).
                $page.post(basePath + 'xrnote/anchors/' + nid, {
                  op: 'save',
                  uuid: uuid,
                  note_nid: d.note_nid,
                  selector: JSON.stringify({
                    pos: { start: start, end: end },
                    quote: { exact: exact, prefix: prefix, suffix: suffix }
                  })
                })
                .fail(function (xhr) {
                  if (win.top && win.top.console) {
                    win.top.console.error('XRNote anchor save failed', xhr.status, xhr.responseText);
                  }
                  alert('XRNote: anchor save failed (HTTP ' + xhr.status + '). See log/console.');
                });

              } catch (err) {
                if (win.top && win.top.console) win.top.console.error('[xrnote] onInsert error', err);
                alert('XRNote onInsert error: ' + (err && err.message ? err.message : err));
              }
            }, 0);
          }

          // Bind one-shot handler.
          $doc.off('xrnote-insert.' + uuid, onInsert).on('xrnote-insert.' + uuid, onInsert);

          // Open Backdrop AJAX dialog via injected link.
          if (!BD || !BD.attachBehaviors) {
            alert('XRNote: Backdrop.attachBehaviors() not available (missing backdrop.ajax/backdrop.dialog.ajax on page).');
            return;
          }

          var $a = $page('<a class="use-ajax" data-dialog="true" style="display:none"></a>')
            .attr('href', url)
            .attr('data-dialog-options', JSON.stringify({
              width: 520,
              modal: true,
              title: 'Insert XRNote'
            }))
            .appendTo($page('body'));

          // CRITICAL: attach behaviors so "use-ajax" is wired up.
          BD.attachBehaviors($a[0]);

          // Trigger click to open the dialog.
          $a.trigger('click');

          // Cleanup.
          setTimeout(function () { $a.remove(); }, 2000);
        }
      });

      return {};
    });
  }

  register();

})(jQuery, Backdrop, window);
