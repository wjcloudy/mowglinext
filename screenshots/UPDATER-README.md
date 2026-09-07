# PR 540 updater screenshots

All 28 host-updater screenshots were regenerated on 7 September 2026 from the
final stack/per-service version UI in feat/settings-updates. Reproduce with
`cd gui/web && npx playwright test tests/e2e/host-updater.spec.ts --workers=2`.

Every image simulates upstream mowglinext/mowglinext. Production v1.2.0 installed,
v1.3.0 available and v1.2.1 component overrides are fixtures, as are the Development
revisions, custom branch and optional camera/helpers. These are not claims about
published releases, live robot data or hardware acceptance.

Desktop: 1440x1800. Mobile: 390x844, scrolled to the relevant stack/settings controls.
The review keeps confirmation/cancellation visible while details scroll. Includes
Simple, Advanced, review, mixed combinations, ROS/GUI/GPS/LiDAR/future-service
selectors, empty release lists, membership add/remove/keep/local and custom branch.

The older live-update-checks files are historical captures of the previous manual
comparison screen and are not linked as current screenshots by PR 540.


Four additional notification screenshots show the unread badge and opened bell
on the Home page (desktop 1440x1000, mobile 390x844). They simulate an upstream
Production v1.3.0 deployment notice and updater notice, not live availability.
The mobile panel is kept within the viewport; Open Updates navigates to settings
without checking remotely or installing. Total current PR screenshots: 32.

All 32 current screenshots were refreshed after the Simple-mode wording change:
Running / After update. Follow selected release remains an Advanced selector.
