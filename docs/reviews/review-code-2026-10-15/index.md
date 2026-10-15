# review-code 2026-10-15 (UTA-0329)

`review-code --since c352632`: the commit the 2026-09-26 sweep reviewed.
Twelve lanes, each holding only source files changed since then, whole.
Test files and CMake files were not reviewed; code unchanged since
c352632 was not re-read. Each lane file is that lane's return, unedited.
A finding is cited as `lane-NN:line`. Dispositions are on the roadmap
items whose `Source:` is `review-code-2026-10-15`.

## Lane cost

| lane | turns | cost_usd | cache_read |
|---|---|---|---|
| lane-01-ubake-core | 24 | 1.758 | 737303 |
| lane-02-ubake-light | 33 | 1.700 | 1058049 |
| lane-03-ubundle | 42 | 2.058 | 1542075 |
| lane-04-urender-frame | 27 | 2.179 | 1775049 |
| lane-05-urender-resources | 21 | 1.732 | 596797 |
| lane-06-urender-shaders | 29 | 1.816 | 1016278 |
| lane-07-upkg-core | 24 | 1.484 | 991504 |
| lane-08-content-libs | 27 | 1.837 | 711787 |
| lane-09-app | 22 | 1.324 | 481214 |
| lane-10-tools-dump-paths-bake | 18 | 1.486 | 907560 |
| lane-11-tools-bench-shot | 31 | 1.641 | 927069 |
| lane-12-scripts | 31 | 1.679 | 872451 |
