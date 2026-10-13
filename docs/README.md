# CS 1.6.1 documentation

Four documents, each one self-contained. Read the first two if you want to know what this
project actually does; the third if you want to write code against it.

| Document | What it covers | Read it if you want to… |
|---|---|---|
| [**HANDS.md**](HANDS.md) | The VR Mod: its own arms and weapon viewmodel, hand IK, fingers, the holster/grab/drop state machine, two-handed grip, barrel aim — and how the weapon fit is *measured* instead of eyeballed | …understand or change the VR body |
| [**BOTBRAIN.md**](BOTBRAIN.md) | The two-brain bot AI: the ReGameDLL **advisor** module (fast, in the game DLL) plus the **Jev** sidecar (slow, off-process, LLM-driven), and the file protocol between them | …understand or change how the bots think |
| [**MODS.md**](MODS.md) | The **Mod ABI** reference: the contract between the engine and an external Mod, every hook, every struct field, capability gating, and the rules a Mod must follow | …write your own Mod |
| [**BUILD.md**](BUILD.md) | Reproducing the whole stack from upstream sources: content, patches, the four builds, the Mods | …build it yourself |

Also worth reading, at the repository root:

- [**README.md**](../README.md) — what the project is, screenshots, quick start.
- [**PLAN.md**](../PLAN.md) — the roadmap *and* the engineering log: every phase with its
  acceptance criteria, the measured results, the root cause of each bug that was found, and
  what is still unverified. This is the most honest document in the repository.
- [**CREDITS.md**](../CREDITS.md) — third-party assets, libraries and licenses.
- [**patches/**](../patches/) — the changes this project makes to its upstreams, each with
  its own README and base revision.

## Reference environment

The project was built and tuned on **one machine**: Fedora Linux, rootless Podman (no sudo
anywhere), NVIDIA RTX 4060, and a Quest 3 streamed with WiVRn over Wi-Fi. Numbers quoted in
these documents — frame budgets, streaming bitrates, IPD calibration, the arm/weapon
measurements — come from that setup. The code is not tied to it, but the *defaults* in the
launchers and the `.cfg` files are, and the documents say so where it matters.

## Screenshots in this documentation

The images under [`img/`](img/) are not screenshots of a game session: they are **contact
sheets rendered by the VR Mod's own diagnostic modes**, through the same drawing path the
headset uses. That is deliberate — they are the evidence for claims that would otherwise be
"trust me", and they can be regenerated on any machine without a headset. The recipe is in
[HANDS.md](HANDS.md).
