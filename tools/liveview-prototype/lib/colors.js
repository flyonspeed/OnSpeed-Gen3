// M5GFX color tokens, indexed by their TFT_* names.
// Values are CSS variable names so the active theme controls the actual color.
// The hex values defined in style.css under :root and [data-theme="light"]
// are tuned to match the M5GFX constants closely enough that the rendered
// output at panel-mount distance is indistinguishable from the hardware.
export const colors = Object.freeze({
  TFT_BLACK:      'var(--panel-bg)',
  TFT_WHITE:      'var(--white)',
  TFT_GREEN:      'var(--green)',
  TFT_YELLOW:     'var(--yellow)',
  TFT_RED:        'var(--red)',
  TFT_GREY:       'var(--grey)',
  TFT_DARKGREY:   'var(--dark-grey)',
  TFT_LIGHTGREY:  'var(--light-grey)',
  TFT_CYAN:       'var(--sky)',
  TFT_BROWN:      'var(--ground)',
  TFT_MAGENTA:    'var(--magenta)',
  TFT_ORANGE:     'var(--orange)',
  TFT_BLUE:       'var(--blue)',
});
