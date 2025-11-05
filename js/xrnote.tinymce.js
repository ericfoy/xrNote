(function ($, Backdrop, win) {
  function openDialog(url, title) {
    $.get(url).done(function (html) {
      var $d = $('<div class="xrnote-dialog"></div>').append(html).appendTo('body');
      $d.dialog({ title: title || 'Insert XRNote', modal: true, width: 520, close: function(){ $d.remove(); } });
    });
  }

  function register() {
    if (!win.tinymce || !win.tinymce.PluginManager) return setTimeout(register, 200);
    tinymce.PluginManager.add('xrnote', function (editor) {
      editor.ui.registry.addButton('xrnote', {
        text: 'XRNote',
        onAction: function () {
          var sel = editor.selection, rng = sel.getRng();
          var exact  = sel.getContent({ format: 'text' }) || '';
          var textAll = editor.getContent({ format: 'text' }) || '';
          var start = rng.startOffset || 0, end = rng.endOffset || start;
          var prefix = textAll.substr(Math.max(start - 20, 0), 20);
          var suffix = textAll.substr(end, 20);

          var uuid = 'xr-' + Date.now() + '-' + Math.floor(Math.random()*1e6);
          var st = Backdrop.settings.xrnote || {};
          var url = st.basePath + 'xrnote/modal/add/' + st.nid + '/' + uuid;

          openDialog(url, 'Insert XRNote');

          function onInsert(ev) {
            var d = ev.detail || {};
            if (d.uuid !== uuid || !d.note_nid) return;
            var marker = '<span class="xrnote-anchor" contenteditable="false" data-xr-uuid="'+uuid+'" data-note-nid="'+d.note_nid+'"></span>';
            editor.insertContent(marker);
            $.post(st.basePath + 'xrnote/anchors/' + st.nid, {
              op: 'save',
              uuid: uuid,
              note_nid: d.note_nid,
              selector: JSON.stringify({ pos:{start:start,end:end}, quote:{exact:exact,prefix:prefix,suffix:suffix} })
            });
            win.removeEventListener('xrnote-insert', onInsert);
          }
          win.addEventListener('xrnote-insert', onInsert);
        }
      });
      return {};
    });
  }
  register();

})(jQuery, Backdrop, window);
