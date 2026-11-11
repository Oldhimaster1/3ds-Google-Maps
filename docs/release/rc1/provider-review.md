# Provider review — 11 November 2026

This is a technical release gate, not a legal clearance or a guarantee of service.
The application MIT license does not grant rights to third-party imagery.

| Provider | Evidence / current behavior | Remaining release gate |
| --- | --- | --- |
| NASA GIBS / Blue Marble | [API guidance](https://nasa-gibs.github.io/gibs-api-docs/map-library-usage/); one on-demand tile, historical composite, visible attribution. A live desktop request decoded successfully. | Verify rendering and certificate chain on 3DS. No claim of real-time photography. |
| Google Street View | [Policies](https://developers.google.com/maps/documentation/streetview/policies) restrict access, storage and reuse. Existing unofficial photometa/tile code is preserved as experimental; the new curated dataset contains no Google IDs or images. | **BLOCKER:** an unofficial working endpoint is not authorization. Obtain an appropriate permitted integration or exclude it from production. Classic Google-only StreetGuess shares this blocker. |
| Bing Streetside | [Terms](https://www.bingmapsportal.com/terms) require documented access and attribution. Existing experimental direct endpoint remains. | **BLOCKER:** verify authorized access and complete attribution or exclude from production. |
| OSM raster tiles | [Tile policy](https://operations.osmfoundation.org/policies/tiles/) forbids bulk/offline downloading from the standard public tile service. Existing offline packs use separately configured permitted sources such as USGS. | Audit normal prefetch/cache behavior and provider-specific disk caching before production. OSM data licensing is distinct from tile-server permission. |
| Nominatim | [Usage policy](https://operations.osmfoundation.org/policies/nominatim/) requires identification, caching, user-triggered queries, a switchable endpoint, and an aggregate application request limit. Existing local cooldown is not an aggregate limit across all installations. | **BLOCKER for broad distribution:** establish a sustainable permitted service configuration; do not describe per-device throttling as full compliance. |
| Overpass / OSM buildings | [Operator resource guidance](https://dev.overpass-api.de/overpass-doc/en/preface/commons.html); existing bounded opt-in request, real footprints, OSM attribution. | Shared capacity can reject requests. Test real responses and keep unavailable status; do not continuously poll. No production SLA. |
| Panoramax | [Federated catalog and licenses](https://docs.panoramax.fr/federated-catalog/); per-image license metadata is authoritative. v1 challenge manifest records eight exact STAC item URLs, credits, Etalab 2.0 license IDs, image dimensions and hashes from decoded SD imagery. | Preserve creator attribution and [Open Licence 2.0 terms](https://alliance.numerique.gouv.fr/licence-ouverte-open-licence/). Images can disappear; never substitute challenge IDs silently. Full 3DS path remains unverified. |
| Mapillary | [Image licensing](https://help.mapillary.com/hc/en-us/articles/115001770409-CC-BY-SA-license-for-open-data); existing optional user-supplied token, no new mandatory account. | API access terms and attribution/profile links must be reviewed in addition to the image license. Token permissions are user-specific. |
| KartaView | [Terms](https://kartaview.org/terms) distinguish MIT source from CC BY-SA 4.0 street imagery. | Verify API availability, creator attribution and any share-alike obligations for exported media. |
| OSRM | [Demo service](https://github.com/Project-OSRM/osrm-backend/wiki/Demo-server); existing route line, progress and reroute, with cooldown. | Public demo has no production SLA. No tested spoken or maneuver-by-maneuver turn-by-turn guidance is claimed. |
| Terrain tiles | [Mapzen elevation project](https://github.com/tilezen/joerd); selected-location Terrarium data and original dataset credits. Five public test areas requested from the desktop host. | Verify 3DS TLS, scaling, coastlines and tile edges on hardware. |

## HTTPS and local GPS security

All curl handles now use a bundled Mozilla CA list, peer verification and hostname
validation. HTTPS redirects are restricted to HTTPS. The old httpc redirect path
hands an HTTPS target to verified curl. No code intentionally disables certificate
validation. Invalid trust, hostname or console date must fail closed.

The currently supplied devkitPro curl package is **8.4.0**. That is an old release;
its [mbedTLS IP-address verification advisory](https://curl.se/docs/CVE-2024-2466.html)
and other applicable advisories must be resolved with a supported updated port
before a security-cleared production release. Correct setopt flags alone are not
sufficient to certify the dependency. The root list was retrieved from
https://curl.se/ca/cacert.pem; retain Mozilla source/license notices in that file.

Local phone GPS now generates a fresh P-256 certificate identity and independent
128-bit bearer secret for each server session. The secret is in the QR URL fragment,
removed from the address bar by the page, and required in the Authorization header.
Duplicate/missing/incorrect headers are rejected. Certificate serial entropy is
independent of the secret. No permissive CORS response is sent. Requests time out
and retries can occur on subsequent location updates. Stopping/restarting invalidates
old pairing URLs. Network logs no longer record request URLs containing coordinates
or tokens, and the phone status omits exact coordinates.

**Remaining security/compatibility gate:** browsers do not automatically trust this
local self-signed certificate. This candidate does not implement a browser-verifiable
pinned certificate onboarding flow. Test the trust process, certificate inspection,
location permission and reconnecting on actual phones. Pair only on a trusted local
network; do not expose the listener to the Internet. The QR itself is a session
credential and must be excluded from public screenshots. Ordinary map screenshots
may still reveal location; there is no universal screenshot-redaction mode.
