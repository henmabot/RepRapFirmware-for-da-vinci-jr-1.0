# Vendored dependencies

| Dependency           | Source                      | Branch  | Date vendored | Commit hash                              |
| -------------------- | --------------------------- | ------- | ------------- | ---------------------------------------- |
| RepRapFirmware       | Duet3D/RepRapFirmware       | 3.6-dev | 2026-09-08    | 8b1d7b31b3d80bb029f4b96a2fc8419bc7346086 |
| CANLib               | Duet3D/CANLib               | 3.6-dev | 2026-09-08    | f0a4c6d53d0cb3ccff8cc4309c2b4b67e158efab |
| CoreN2G              | Duet3D/CoreN2G              | 3.6-dev | 2026-09-08    | c018a2d6b75c072d8513765baa9164e67fd20315 |
| FreeRTOS             | Duet3D/FreeRTOS             | 3.6-dev | 2026-09-08    | f3f427a673253c0d48f3c8c2e090ef0ac31b7db3 |
| RRFLibraries         | Duet3D/RRFLibraries         | 3.6-dev | 2026-09-08    | d10185e12ff242430b4a306eba8569e380cc165b |
| WiFiSocketServerRTOS | Duet3D/WiFiSocketServerRTOS | main    | 2026-09-08    | aab996d50a538d72d41bd86e52346e03a2a3f2aa |

## Bumping a dependency

1. Clone the upstream repo elsewhere at the desired branch/tag.
2. Diff against the currently vendored copy to see what changed.
3. Merge the upstream changes and be careful about these:

| Change | Reason |
| ------ | ------ |
|        |        |

> for now the table is empty since there is no vendored changes to report

4. Update the table above with the new source and date.
