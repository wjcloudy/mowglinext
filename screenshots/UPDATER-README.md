# PR 540 updater screenshots

All 39 current screenshots were regenerated on 7 September 2026 for the simplified
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

Custom branch examples use real upstream `feat/gui-dashboard-improvements`,
verified 2026-09-07: head f7e6f75ab8405c43ee1ddda3d946e029759b47d6
(2026-05-03T10:17:49Z), parent 30b3faa9a7aea7ea7aea20deab06c5f9cc24939f
(2026-05-03T10:10:38Z). Dev examples use 4fc91c4fe15d3bdaaab7f12503c3646c94162187
and parent 1076cdebaecd0f18dd4509c7d90edd33aa0ede14, verified the same day.
Branch names and commit IDs are real; deployment availability, image contracts,
installed identities and publication dates remain simulated (commit dates are
used for illustrative build timestamps). No claim that these branches publish
compatible updater assets is made. The one-component example keeps the installed
base of the custom branch and selects its newer GUI build. Dev base plus GUI
from another branch is unsupported and is not depicted as installable.
