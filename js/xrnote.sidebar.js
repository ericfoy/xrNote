(function ($, Backdrop, win) {
  Backdrop.behaviors.xrnoteSidebar = {
    attach: function (ctx) {
      var $sidebar = $('#xrnote-sidebar', ctx).once('xrnote');
      if (!$sidebar.length) return;

      // Sidebar updates can be triggered frequently (TinyMCE mutates the DOM a lot).
      // Throttle updates to one per animation frame and make sure we observe the
      // *editor iframe* document, not the host page.

      var rafPending = false;
      var lastSignature = null;
      var observer = null;
      var bindTimer = null;

      function getEditorDoc() {
        // Prefer TinyMCE's active editor iframe if available.
        var ed = (win.tinymce && win.tinymce.activeEditor) ? win.tinymce.activeEditor : null;
        var iframe = (ed && ed.iframeElement) ? ed.iframeElement : null;
        // Fallback to DOM queries (Backdrop TinyMCE can vary by version).
        if (!iframe) {
          iframe = win.document.querySelector('iframe.tox-edit-area__iframe') ||
                   win.document.querySelector('iframe.mce-edit-area');
        }
        return (iframe && iframe.contentDocument) ? iframe.contentDocument : null;
      }

      function refreshNow() {
        var doc = getEditorDoc();
        if (!doc || !doc.documentElement) return;

        var anchors = doc.querySelectorAll('.xrnote-anchor');
        var items = [];
        for (var i = 0; i < anchors.length; i++) {
          var el = anchors[i];
          var r = el.getBoundingClientRect();
          items.push({
            top: r.top + (doc.documentElement.scrollTop || 0),
            uuid: el.getAttribute('data-xr-uuid') || '',
            nid:  el.getAttribute('data-note-nid') || ''
          });
        }
        items.sort(function (a, b) { return a.top - b.top; });

        // Avoid unnecessary DOM writes (which can cascade into more observers).
        var sig = items.map(function (i) { return i.uuid + ':' + i.nid; }).join('|');
        if (sig === lastSignature) return;
        lastSignature = sig;

        $sidebar.html(items.map(function (i) {
          return '<div class="xrnote-item" data-uuid="' + i.uuid + '">Note ' + i.nid + '</div>';
        }).join(''));
      }

      function scheduleRefresh() {
        if (rafPending) return;
        rafPending = true;
        (win.requestAnimationFrame || function (cb) { return setTimeout(cb, 16); })(function () {
          rafPending = false;
          refreshNow();
        });
      }

      function bindObserver() {
        var doc = getEditorDoc();
        if (!doc || !doc.body) return false;
        if (observer) observer.disconnect();
        observer = new MutationObserver(scheduleRefresh);
        observer.observe(doc.body, { childList: true, subtree: true, characterData: true });
        $sidebar.data('xrnoteObserver', observer);
        return true;
      }

      // Initial paint.
      scheduleRefresh();

      // Scroll can also affect anchor vertical positions.
      $(win).on('scroll.xrnote', scheduleRefresh);

      // TinyMCE may initialize after behaviors attach. Poll briefly until the iframe exists.
      if (!bindObserver()) {
        var tries = 0;
        bindTimer = setInterval(function () {
          tries++;
          if (bindObserver() || tries > 50) {
            clearInterval(bindTimer);
            bindTimer = null;
          }
        }, 200);
        $sidebar.data('xrnoteBindTimer', bindTimer);
      }
    },
    detach: function (ctx, settings, trigger) {
      if (trigger !== 'unload') return;
      $(win).off('scroll.xrnote');
      var $sidebar = $('#xrnote-sidebar', ctx);
      var ob = $sidebar.data('xrnoteObserver');
      if (ob && ob.disconnect) ob.disconnect();
      var t = $sidebar.data('xrnoteBindTimer');
      if (t) clearInterval(t);
    }
  };
})(jQuery, Backdrop, window);
