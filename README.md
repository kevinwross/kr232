# kr232

A 32-bit teaching virtual machine: `kr216` 1.0 widened to 32-bit registers and a 4 GiB address
space, with a protected kernel mode and a host-rendered display, console, keyboard and graphics
command set shaped after `kr32`.

**Status.** The specification was approved on 2026-11-22. Implementation is at M0: the contracts are
being written, and nothing runs yet.

| Document | Version | Holds |
|---|---|---|
| [kr232-isa.md](kr232-isa.md) | 0.4 | the machine: registers, encodings, traps, the memory map, `SYS` |
| [kr232-devices.md](kr232-devices.md) | 0.4 | device registers, the display, the guest packages |
| [kr232-plan.md](kr232-plan.md) | 0.17 | milestone scope, order and exit criteria |
| [kr232-impl.md](kr232-impl.md) | 0.1 | tasks, commands, hosts, the log and gate records |

## Check it

Node is the only requirement, and the lint is the only executable check in this repository:

```sh
node tools/check-isa.mjs
```

It prints one line and exits 0 when the two drafts agree with each other: Appendix A's encoding
vectors round-trip, Appendix B covers every first byte, Appendix C tiles the 4 GiB map, both device
page tables tile their window, and every table is well formed.

## Build it

There is no build yet. `kr232-emu` arrives with M1, and the implementation plan carries its commands,
its presets and its hosts.

## Read next

[AGENTS.md](AGENTS.md) carries the standing rules; [AGENTS.local.md](AGENTS.local.md) carries this
repository's own. The two drafts are the authority for the machine, the plan for the milestones, and
the implementation plan for the work in front of you.
