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
    if (!win.tinymce || !win.tinymce.PluginManager) return setTimeout(register, 200);
    tinymce.PluginManager.add('xrnote', function (editor) {
      editor.ui.registry.addButton('xrnote', {
        text: 'XRNote',
        onAction: function () {

          // SMOKE TEST: prove the click handler runs.
          editor.insertContent('@');
          if (win.console) console.log('XRNote button clicked');
          //return; // <- remove/comment this line after the test

          var sel = editor.selection, rng = sel.getRng();
          var exact  = sel.getContent({ format: 'text' }) || '';
          var textAll = editor.getContent({ format: 'text' }) || '';
          var start = rng.startOffset || 0, end = rng.endOffset || start;
          var prefix = textAll.substr(Math.max(start - 20, 0), 20);
          var suffix = textAll.substr(end, 20);

          var uuid = 'xr-' + Date.now() + '-' + Math.floor(Math.random()*1e6);
          var st = Backdrop.settings.xrnote || {};
          var url = st.basePath + 'xrnote/modal/add/' + st.nid + '/' + uuid;

          if (win.console) console.log('XRNote settings/url', { st: st, url: url });

          openDialog(url, 'Insert XRNote');

          // Use the top window jQuery if available (works even if TinyMCE is in an iframe).
          var $top = (win.top && win.top.jQuery) ? win.top.jQuery : $;
          var $root = $top('body');

          // Listen for the server-triggered jQuery event.
          function onInsert(e, d) {
            d = d || {};
            if (d.uuid !== uuid || !d.note_nid) return;

            if (win.console) console.log('XRNote insert event received', d);

            var marker = '<span class="xrnote-anchor" data-xr-uuid="' + uuid +
                        '" data-note-nid="' + d.note_nid + '">[XR]</span>';
            editor.insertContent(marker);

            $.post(st.basePath + 'xrnote/anchors/' + st.nid, {
              op: 'save',
              uuid: uuid,
              note_nid: d.note_nid,
              selector: JSON.stringify({ pos:{start:start,end:end}, quote:{exact:exact,prefix:prefix,suffix:suffix} })
            });

            // Close the dialog opened by openDialog().
            if ($top.fn && $top.fn.dialog) {
              $top('.xrnote-dialog').dialog('close');
            }

            // One-shot unbind (namespace by uuid so multiple clicks don’t conflict).
            $root.off('xrnote-insert.' + uuid);
          }

          $root.on('xrnote-insert.' + uuid, onInsert);
        }
      });
      return {};
    });
  }
  register();

})(jQuery, Backdrop, window);
