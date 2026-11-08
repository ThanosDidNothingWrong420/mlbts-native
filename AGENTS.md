\# AGENTS.md — MLB The Show 26 PC Recomp



\## Project Goal



Create a native Windows 11 x86-64 static recompilation of the \*\*Nintendo Switch version of MLB The Show 26\*\*, targeting offline gameplay.



The initial goal is a playable experience covering:



\- Exhibition games

\- Road to the Show

\- Franchise

\- Local progression and save data

\- Menus and other offline functionality



Online features and Diamond Dynasty are out of scope initially.



\*\*Target game version: 1.0.15 (Switch version 1.015).\*\* Do not silently switch to a different update version.



\## Reference Projects



Study these projects before designing or implementing major components:



1\. \*\*TotK PC Recomp\*\*

&#x20;  \[https://github.com/valentderah/totk-pc-recomp](https://github.com/valentderah/totk-pc-recomp)



&#x20;  Primary architectural reference. Investigate its ARM64 recompilation pipeline, runtime, Switch SDK compatibility layer, graphics translation, shader handling, and build system.



2\. \*\*Astral Chain PC Recomp\*\*

&#x20;  \[https://github.com/captainj-sparrow/astral-chain-pc-recomp](https://github.com/captainj-sparrow/astral-chain-pc-recomp)



&#x20;  Secondary reference for another Switch-to-Windows static recompilation built on similar foundations. Compare its implementation choices and identify reusable improvements.



3\. \*\*Power Moon Recomp\*\*

&#x20;  \[https://gitlab.com/zzzarathustra-dev/power-moon-recomp](https://gitlab.com/zzzarathustra-dev/power-moon-recomp)



&#x20;  Additional reference for static recompilation architecture and implementation techniques.



These projects are references, not drop-in solutions. Their extraction pipelines, game-specific assumptions, title IDs, module layouts, and compatibility implementations must not be assumed to work with MLB The Show 26.



\## Development Environment



\- OS: Windows 11

\- CPU: AMD Ryzen 5 5600X

\- GPU: NVIDIA RTX 3060 Ti

\- Target: Windows x86-64

\- Graphics backend: Investigate Vulkan compatibility and requirements before committing to an implementation.



\## Initial Priorities



Before attempting a full recompilation, perform technical reconnaissance of the supplied game files.



Determine:



1\. Game title ID, build metadata, and exact version.

2\. Executable modules, their sizes, hashes, and relationships between base game and update.

3\. ARM64 architecture details and instruction coverage required.

4\. Imported Switch SDK functions, system services, and SVC usage.

5\. Graphics API, shader formats, and GPU requirements.

6\. Audio, filesystem, input, threading, and memory requirements.

7\. Which components from the reference projects can realistically be reused.



Produce a concise technical report documenting findings, uncertainties, compatibility gaps, and recommended next steps.



Do not begin large-scale implementation based on unverified assumptions.



\## Engineering Guidelines



\- Prioritize correctness and reproducibility over premature optimization.

\- Investigate existing implementations before reinventing infrastructure.

\- Keep generic recompilation/runtime infrastructure separate from MLB-specific code.

\- Do not assume MLB uses the same engine, SDK version, graphics architecture, or runtime behavior as the reference games.

\- Verify instruction support, system calls, and imported functions against the actual game binaries.

\- Implement missing functionality incrementally and test each component independently.

\- Clearly distinguish confirmed findings from hypotheses.

\- Document architectural decisions and significant discoveries.

\- Avoid unnecessary abstractions and overengineering.



\## Repository and Asset Safety



\- Never commit copyrighted game files, extracted game assets, encryption keys, or other sensitive material.

\- Keep all game dumps and local extraction artifacts outside version control.

\- Add appropriate `.gitignore` rules before generating large amounts of extracted data.

\- Do not upload proprietary game files to external services.

\- Keep the project buildable and its research reproducible without distributing the original game files.



