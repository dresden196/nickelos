// Getting help: the email address appears only when someone clicks, so the
// bots that collect addresses from pages (and from the site's source) never
// see it. Then the link is a normal mailto: link.
"use strict";
document.querySelectorAll("a.email-reveal[data-e]").forEach(function (a) {
  a.addEventListener("click", function (e) {
    if (a.dataset.shown) return;
    e.preventDefault();
    var addr = atob(a.dataset.e).split("").reverse().join("");
    a.href = "mailto:" + addr + "?subject=NickelOS";
    a.textContent = addr;
    a.dataset.shown = "1";
  });
});
