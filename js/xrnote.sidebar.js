(function ($, Backdrop, win) {
  Backdrop.behaviors.xrnoteSidebar = {
    attach: function (ctx) {
      var $sidebar = $('#xrnote-sidebar', ctx);
      if (!$sidebar.length) return;

      // Works for both edit (TinyMCE iframe) and view (inline).
      function getDoc() {
        var iframe = $('iframe.tox-edit-area__iframe', ctx)[0] || $('iframe.mce-edit-area', ctx)[0];
        return iframe ? iframe.contentDocument : document;
      }

      function refresh() {
        var doc = getDoc();
        var $anchors = $(doc).find('.xrnote-anchor');
        var items = [];
        $anchors.each(function () {
          var r = this.getBoundingClientRect();
          items.push({ top: r.top + (win.pageYOffset||doc.documentElement.scrollTop), uuid: this.getAttribute('data-xr-uuid'), nid: this.getAttribute('data-note-nid') });
        });
        items.sort(function(a,b){ return a.top - b.top; });
        $sidebar.html(items.map(function(i){ return '<div class="xrnote-item" data-uuid="'+i.uuid+'">Note '+i.nid+'</div>'; }).join(''));
      }

      refresh();
      $(win).on('scroll', refresh);
      var doc = getDoc();
      if (doc && doc.body) new MutationObserver(refresh).observe(doc.body, {childList:true, subtree:true});
    }
  };
})(jQuery, Backdrop, window);
