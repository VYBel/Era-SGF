# Custom Menus Implementation Plan

**Goal:** собственные меню с единым заголовком и доступными пунктами.
**Architecture:** MenuViewModel → Scene; Application owns menu state and navigation.
**Tech Stack:** existing C++14 / InkView, no new dependencies.
**Spec:** ../specs/2026-11-05-custom-menus-design.md

## Review Focus
- Back versus close must preserve board position and correct parent.
- All rows in long lists reachable in either orientation.
- Wake/rotation preserve open menu; no board taps leak through.
- Static menus have no timers; file confirmation and keyboards still work.
- Long titles do not overlap header buttons.

## Tasks
- [x] Add red UI test using real taps for custom menu/back/close and lifecycle.
- [x] Implement shared header and make_menu_scene(MenuViewModel, width, height),
      menu_page_capacity; test geometry and paging at 320, 600 and Era sizes.
- [x] Replace OpenMenu with Application state, page/focus handling; adapt tests
      from native callbacks to real rendered text taps; cover long lists and buttons.
- [x] Run full tests, sanitizers, ARM/SDK builds and independent review.
- [x] Capture SDK screens, update version/docs, backup/install/verify.
- [ ] Publication after this commit: push, CI, tag/release and download verification.
