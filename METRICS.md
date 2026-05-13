# Traffic / clone / download metrics

GitHub already provides built-in traffic analytics for repositories.

## Built-in GitHub traffic (recommended)

If you have **push access** to this repository, you can view the last **14 days** of:
- Views (total + unique)
- **Clones** (total + unique)
- Top referral sources (for web traffic)
- Popular content paths

Open: **Insights → Traffic**

Documentation: https://docs.github.com/en/repositories/viewing-activity-and-data-for-your-repository/viewing-traffic-to-a-repository

### Important limitation
GitHub does **not** provide geographic location for cloners/downloaders, and clone referrers are not available.

## API access (optional)

The same 14-day traffic data is also available via the REST API (requires write access):
- GET /repos/{owner}/{repo}/traffic/views
- GET /repos/{owner}/{repo}/traffic/clones
- GET /repos/{owner}/{repo}/traffic/popular/referrers
- GET /repos/{owner}/{repo}/traffic/popular/paths

Docs: https://docs.github.com/en/rest/metrics/traffic

## External counters

If you want a public counter on the README (views), you can integrate a third-party badge/counter.
Note: this will count page views, not git clones.
