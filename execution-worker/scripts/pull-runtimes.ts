#!/usr/bin/env bun
/**
 * Provision every runtime image in runtimes.json on this host.
 * Images with an `imageBuild` directory are built locally; the rest are pulled.
 * Run once before starting the worker:  bun run pull-runtimes
 */
import { $ } from "bun";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const registry = await Bun.file(join(root, "runtimes.json")).json();

let failed = 0;
for (const r of registry.runtimes) {
  try {
    if (r.imageBuild) {
      console.log(`building ${r.image} from ${r.imageBuild} ...`);
      await $`docker build -t ${r.image} ${join(root, r.imageBuild)}`;
    } else {
      console.log(`pulling ${r.image} ...`);
      await $`docker pull ${r.image}`;
    }
    console.log(`ok: ${r.id}`);
  } catch (e) {
    failed += 1;
    console.error(`FAILED: ${r.id} (${r.image}): ${e instanceof Error ? e.message : e}`);
  }
}
if (failed > 0) {
  console.error(`${failed} image(s) failed; the worker will error on those runtimes until fixed.`);
  process.exit(1);
}
console.log("all runtime images ready");
