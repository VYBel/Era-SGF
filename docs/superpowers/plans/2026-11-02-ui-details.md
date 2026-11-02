# UI details 0.6.0

User explicitly requested cosmetic refinements, terminal result, accessible game information
and About. No network implementation. Existing fullscreen comment tap should be verified.

- Draw navigation arrows as lines with explicit spacing from 10 and endpoint bars.
- Put game information at top left, retain one-line player header and refresh at top right.
- Show decoded RE below the move row only at the main-line leaf; hide absent results and
  do not attribute the game result to an analysis variation. Translate standard RE values.
- Keep existing fullscreen comment behavior and pagination; verify tap/return preserve node.
- About in Settings: version, VYBel, project URL, MIT, third-party acknowledgments; close
  returns to Settings. No extra main-menu item or dependency.
- Verify tests, official SDK visuals/PNG, ARM build; update reader with backup if mounted.
- Audit shipped library licenses separately from SDK terms; document unresolved InkView
  redistribution grant rather than asserting that dynamic linking itself settles it.
