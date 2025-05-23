/*eslint strict: ["error", "global"]*/

function openFullscreen()
{
  // Get an element to show fullscreen (We just use the whole document)
  let fullscreenElement = document.documentElement;

  if (fullscreenElement.requestFullscreen) {
    fullscreenElement.requestFullscreen();
  } else if (fullscreenElement.webkitRequestFullscreen) { // Safari
    fullscreenElement.webkitRequestFullscreen();
  } else if (fullscreenElement.msRequestFullscreen) { // IE11
    fullscreenElement.msRequestFullscreen();
  }
}

function closeFullscreen()
{
  // Tell the document to exit fullscreen
  if (document.exitFullscreen) {
    document.exitFullscreen();
  } else if (document.webkitExitFullscreen) { // Safari
    document.webkitExitFullscreen();
  } else if (document.msExitFullscreen) { // IE11
    document.msExitFullscreen();
  }
}

export function toggleFullscreen()
{
  // https://developer.mozilla.org/en-US/docs/Web/API/Document/fullscreenElement
  const bIsFullscreen = (document.fullscreenElement != null);
  if (bIsFullscreen) {
    closeFullscreen();
  } else {
    openFullscreen();
  }
}
