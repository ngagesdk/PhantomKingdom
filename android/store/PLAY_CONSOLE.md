# Releasing on Google Play

Everything the Play Console asks for, in the order it asks. Texts and
graphics are in `fastlane/metadata/android/` (standard fastlane layout, so
`fastlane supply` can upload them; copying them by hand works as well).

## 0. Before you start

- Google Play developer account. New personal accounts must run a closed
  test with at least 12 testers for 14 days before publishing to
  production; check the current rule in the Play Console.
- Replace `CONTACT_EMAIL` in `PRIVACY.md` and publish it at a public URL
  (for example its GitHub URL).
- Build a signed bundle: see `android/README.md`, "Release signing".
- Rights: the listing must not suggest the app contains or is the original
  game. Keep the "your own game card" wording and the disclaimer; Google may
  ask for proof that you may use the "PhantomKingdom" name and artwork.

## 1. Create the app

| Field | Value |
|---|---|
| App name | PhantomKingdom |
| Default language | English (United States), en-US |
| App or game | Game |
| Free or paid | Free |

## 2. Store listing

| Field | File |
|---|---|
| App name (30) | `en-US/title.txt` |
| Short description (80) | `en-US/short_description.txt` |
| Full description (4000) | `en-US/full_description.txt` |
| App icon 512 x 512 | `en-US/images/icon.png` |
| Feature graphic 1024 x 500 | `en-US/images/featureGraphic.png` |
| Phone screenshots (2 to 8) | `en-US/images/phoneScreenshots/1_*.png` to `6_*.png` (1080 x 2160) |

Add German (Translations > Add > German, de-DE) with the files in `de-DE/`;
the graphics are shared.

Store settings: category *Strategy*; website: your project page; email:
your support address.

## 3. App content

Privacy policy: the URL from step 0. Ads: No.

App access: all functionality is available without special access. Note
for reviewers:

> PhantomKingdom is a reimplementation of the N-Gage game Pocket Kingdom.
> It ships without game data: players import the original game files from
> their own Pocket Kingdom game card, from a ZIP archive or a folder.
> Without game data the start screen works (status, server test, settings,
> log), but the game cannot be started. Online play needs an account on
> the PhantomLink server, which the player creates there.

Content rating (IARC): category *Game*; answer for the game's content
(fantasy strategy); users can interact online: Yes; shares location: No;
purchases: No.

Target audience: 13 and older. Appeals to children: No.

Data safety (the final answers are yours):
- Collects or shares data: Yes. The Arena user name, password and the
  app's random N-Gage ID go to the server the user picks, which Google
  counts as collection even though the developer never receives it.
- Encrypted in transit: No.
- Data types: *Device or other IDs* (the random N-Gage ID) and *Personal
  info > Name* (Arena user name, kingdom name): collected, not shared,
  optional (online play only), purpose *App functionality*. Nothing else.
  The imported game files stay on the device.
- Advertising ID: No.

## 4. Release

1. Testing > Closed testing > Create track, add testers.
2. Create release, upload `app-release.aab`, release name `0.1.0 (1)`,
   notes from `en-US/changelogs/1.txt` (and `de-DE`).
3. Review and roll out; promote to Production after the testing period.

Play App Signing is on by default: Google keeps the signing key, you keep
the upload key. Back the keystore up.

## 5. Each update

1. Raise `versionCode` (and `versionName`) in `app/build.gradle.kts`
   (`PK_VERSION` in `src/main.c` is the game's own version).
2. Add `fastlane/metadata/android/<lang>/changelogs/<versionCode>.txt`.
3. `./gradlew bundleRelease`, upload, roll out.
