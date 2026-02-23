document$.subscribe(function() {
  var feedback = document.forms.feedback;
  if (typeof feedback === "undefined") return;

  feedback.hidden = false;

  feedback.addEventListener("submit", function(ev) {
    ev.preventDefault();

    var page = document.location.pathname;
    var data = ev.submitter.getAttribute("data-md-value");
    var label = data === "1" ? "positive" : "negative";

    if (window.goatcounter && window.goatcounter.count) {
      window.goatcounter.count({
        path:  "feedback/" + label + page,
        title: "Feedback: " + label + " — " + page,
        event: true
      });
    }

    feedback.firstElementChild.disabled = true;

    var note = feedback.querySelector(
      ".md-feedback__note [data-md-value='" + data + "']"
    );
    if (note) note.hidden = false;
  });
});
