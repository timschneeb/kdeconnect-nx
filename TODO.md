* detect sleep mode and disable restarts & client until wakeup (add setting for that; disable connectivity during sleep by default)
* feat: remote mouse pad mode?
* notify when remote device has low battery
* optimization: use libpng (has streaming support) instead of stbi
* fix: don't queue notifications, if:
  * in sleep mode
  * ultrahand notification flag is not set