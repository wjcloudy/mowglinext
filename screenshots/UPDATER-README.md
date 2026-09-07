# PR 540 updater screenshots

All 33 current screenshots were regenerated on 7 September 2026 for the simplified
installed/available release UI, running version footer and complete notification captures.
Reproduce: `cd gui/web && npx playwright test tests/e2e/host-updater.spec.ts --workers=2`.

All images simulate upstream mowglinext/mowglinext. Production v1.2.0 installed,
v1.3.0 available and v1.2.1 component overrides are fixtures, as are Development
revisions, the custom branch and optional camera/helpers. These are not claims
about published releases, live robot data or hardware acceptance.

Simple shows the installed and available release once, with review above the
running stack. Advanced retains all managed-service selectors. Desktop footer
and mobile More show the verified running version, with custom/unknown fallbacks.

Desktop: 1440x1800 (notifications 1440x1000). Mobile: 390x844. Simple screenshots
show the top summary and review action; Advanced screenshots scroll to controls.
Notification captures wait for dashboard content, telemetry, fonts and battery/map
entrance animations. The mobile header keeps all controls visible.

Includes Simple, Advanced, review, mixed combinations, ROS/GUI/GPS/LiDAR/future
service selectors, empty release lists, membership changes, custom branch settings,
unread badge/opened notices and the mobile More summary.
Older live-update-checks images are historical and are not linked as current by PR 540.
