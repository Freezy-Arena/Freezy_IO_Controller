function toggleStatic(checked) {
  var fields = document.querySelectorAll('.static-field');
  fields.forEach(function(f) {
    f.querySelector('input').disabled = checked;
    f.style.opacity = checked ? '0.4' : '1';
  });
}
window.onload = function() {
  var cb = document.getElementById('dhcpToggle');
  toggleStatic(cb.checked);
  cb.addEventListener('change', function() { toggleStatic(this.checked); });
};
