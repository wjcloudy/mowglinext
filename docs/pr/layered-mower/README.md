# Layered mower draft PR screenshots

Captured from the real application with mocked REST/WebSocket telemetry using
`gui/web/tests/e2e/layered-pr-gallery.spec.ts`, Chromium, 1440 x 1250.
No mower was contacted. The settings preview uses the example values below;
the map continues to use the running robot description, not unsaved settings.
These are illustrative installations, not measurements of .118 or new presets.

| Geometry (metres) | Yardforce 500 example | Custom angular (Utility) |
| --- | --- | --- |
| Chassis length / width / height | .600 / .450 / .190 | .660 / .460 / .180 |
| Chassis centre X / bottom Z | .180 / -.050 | .180 / -.060 |
| Drive wheel radius / width / centre track | .100 / .040 / .325 | .100 / .050 / .300 |
| Caster radius / centre track / X | .030 / .300 / .400 | .040 / .280 / .390 |
| GPS X / Y / Z | .150 / 0 / .148 | .160 / 0 / .130 |
| LiDAR X / Y / Z | .310 / 0 / .139 | .320 / 0 / .131 |
| IMU X / Y / Z | .040 / -.090 / .015 | .035 / -.090 / .005 |

All sensor yaw values are zero. Coordinates are relative to the rear axle;
X is forward, Y left and Z up. GPS and LiDAR bases meet the local visible shell roof; the IMU
is enclosed. Drive-wheel outer spans are 365 mm (500) and 350 mm (custom). Wheels and
casters fit within the chassis plan bounds. The 500 retains the shipped chassis
and drive-wheel dimensions, with example caster and sensor installation overrides.
The custom values are screenshot fixtures only and do not alter default settings.

Source shell contours are illustrative, not CAD. The Yardforce rear wheel recess
still needs refinement; see the wheel enclosure audit and HARDWARE_REQUIRED
measurement procedure in `gui/web/src/components/robot/README.md`.

Validation on 2026-10-10: all 942 GUI unit tests in 116 files passed; GUI lint
passed with 898 warnings and zero errors. Two screenshot tests, four Xacro tests,
configuration drift and Go schema/template/model/generated-type parity passed.
Earlier checks on this feature: GUI production build; 12 application Playwright
cases and map projection checks. Full ROS workspace build/test and physical
geometry acceptance have not been completed.


## Chassis collection gallery

`chassis-gallery.png` is a presentation fixture, not an additional application
screen. It uses the production `LayeredMower` renderer with the same
600 x 450 x 190 mm geometry for all five styles, 200 mm drive wheels,
325 mm centre track, 40 mm tyre width and casters at X=390 mm / track=280 mm.
RM1000 uses chassis centre X=150 mm and caster X=360 mm to align its rear
wheel opening with the axle; the other gallery shells retain centre X=180 mm.
Sensors are omitted to make the shell shapes easy to compare. Reproduce with:

```sh
npx playwright test tests/e2e/layered-pr-gallery.spec.ts -g "chassis style gallery" --workers=1
```

The gallery capture test passed and the final screenshot was visually inspected.


## Map views

The map screenshots come from the real Map page with a plain local basemap and
mock telemetry. `app-map-desktop-docked.png` and `app-map-mobile-docked.png`
place the mower and dock at the same base-link pose/heading and show At base.
`app-map-desktop.png` intentionally separates them by 0.8 m to reveal both
assets and uses Idle status. This is not a live mower position report.

The mock published URDF carries the same caster/sensor installation overrides
as the 500 settings example; map geometry comes from that description.
All four desktop/mobile docked/undocked Playwright cases passed, including
coincident docked anchors, compact WebP asset budgets and unchanged artwork
while the mower pose updates. Screenshots were visually inspected.

Sensor-editor screenshots refreshed after the review fixes for independent drag/yaw targets and joint-relative visual origins.


## Unified Hardware appearance update

The gallery now includes five shells plus the shared paired dock. RM1000 was
converted from a static map photo to the same independently scaled shell, wheels,
casters, blade and sensors. Its side profile now follows the user-supplied RM1000 side photograph,
restyled in graphite/mint. It remains an illustration, not a measured model. The model preset's numerical
geometry remains empty. Hardware presets can declare a chassis/dock appearance
pair independently of ROS parameters. Both separate map appearance selectors
are replaced by a Hardware link; legacy dock overrides are no longer read.

`rm1000-hardware.png` and `rm1000-sensors.png` use illustrative 570 Ã— 400 Ã— 190 mm
geometry, chassis centre X=0.145 m and bottom Z=-0.05 m; wheels radius=0.10 m,
width=0.04 m, track=0.30 m; casters radius=0.04 m, X=0.355 m, track=0.26 m.
GPS is at (0.115, 0, 0.145), LiDAR (0.245, 0, 0.145), IMU (0.035, -0.09, 0.005).
The sensor heights meet the visible shell roof in this illustration. They are
not recommended physical RM1000 mounting settings.

`app-map-rm1000-desktop-docked.png` uses the same mock URDF as the earlier 500 map
fixture, deliberately demonstrating that choosing a different shell does not
change robot dimensions or anchors. It seeds the appearance from a legacy
RM1000 selection and verifies that the old static image is not loaded.

Validation of this final update: 51 focused unit tests, 23 application/browser
cases, TypeScript, production build and lint (0 errors, 898 existing warnings).
The final RM1000 capture and gallery were refreshed and individually rechecked.
The earlier merged baseline passed 949 unit tests before the RM1000 conversion;
that number is not a claim about the later removal of obsolete static-menu tests.

## Photo-based RM1000 side and console anchors

The supplied side photograph replaces the previously inferred side silhouette.
The approved top source PNG and compact top WebP are byte-for-byte unchanged.
The screenshot example moves the chassis 35 mm rearward relative to the axle,
with its roof sensors and front casters following the body; wheel radius and
wheel-centre track are unchanged. These are illustrative fixture values only.

Stop buttons are SVG layers, not generated into the shell images. Each style now
specifies its own rear-console position and local roof height, in visible-shell
fractions. Top and side share the same fore/aft position; solid and transparent
modes retain the same anchor. They do not change URDF geometry or robot controls.

Validation: 16 renderer tests, TypeScript and targeted ESLint passed. All 22
Hardware/Sensors/gallery/map browser cases passed after the side and anchor edits.
Application screenshots use mocked telemetry; no robot was contacted.
