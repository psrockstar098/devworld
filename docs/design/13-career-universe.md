# DEVWORLD — Stage 6: Complete technology career universe

Status: **spec locked**. This stage builds the content and practical
environments for the technologies players can learn and use. It is the
content stage: it assumes the Stage 4 learning engine and the Stage 5
progression engine, and fills them with real tracks.

> Note: the pasted spec listed §6.5 twice, identically. It is recorded once
> here.

Objective: Give players a broad, connected universe of real technology
careers.

## 6.1 Programming language tracks

Each language needs its own curriculum, runtime, tooling, assessment
library, projects and career opportunities.

| Language or family | Main engineering opportunities |
|---|---|
| Python | Automation, backend services, data science and AI |
| JavaScript / TypeScript | Web applications, frontend and full-stack systems |
| C / C++ | Systems software, graphics, performance and game engines |
| Rust | Systems programming, safe concurrency and infrastructure |
| Java / C# | Enterprise systems, backend development and application software |
| Go | Cloud services, infrastructure and networked applications |
| SQL | Data analysis, database design and query optimization |
| Swift / Kotlin | Mobile applications and platform development |
| PHP | Web backends and content-driven applications |
| Assembly | Instruction sets, low-level programming and reverse engineering |
| HTML / CSS | Web structure, styling and accessible interfaces |

Players should be able to move from language syntax to idiomatic code,
libraries, testing, profiling and real projects.

## 6.2 Frameworks and development ecosystems

Build learning and project environments for relevant technologies such as:

- React and other frontend frameworks.
- Node.js and backend frameworks.
- Django and FastAPI.
- .NET and Spring.
- Game engines such as Godot and Unreal Engine.
- Mobile development toolchains.
- Build systems, package managers and dependency management.
- Cloud, container and deployment tooling.

Framework availability should be tied to the player's demonstrated
prerequisites, not simply the overall level.

## 6.3 Professional disciplines

Create full career tracks for:

- Frontend, backend and full-stack engineering.
- Mobile and desktop application development.
- Database and data engineering.
- Game development and game engine programming.
- AI and machine learning.
- Cybersecurity and defensive security engineering.
- Embedded systems and firmware.
- Networking and telecommunications.
- Audio, video and graphics software.
- Cloud infrastructure and DevOps.
- Compiler construction and programming language design.
- Operating systems and distributed systems.
- Robotics, electronics and automation.

## 6.4 Shared technical foundations

Build reusable lessons for mathematics, discrete structures, algorithms,
computer architecture, debugging, software testing, system design and
technical communication.

## 6.5 Career-specific projects

Every track should have a project ladder:

- Small guided exercises.
- A standalone working component.
- A project combining several concepts.
- A project built from specifications.
- A team project.
- An independently designed capstone.

## Deliverables

Language catalog, framework catalog, career trees, technical curricula,
project libraries and specialist assessment suites.

## Completion criterion

Every advertised career path has actual learning content, appropriate
tools, assessed competencies and projects that demonstrate its skills.

## §7 — What is already there (checked, not rebuilt)

| Piece | State |
|---|---|
| Career system (paths, eligibility with reasons, respecialization with history) | Built in the shell (`careers.*`, entry requirements reuse the unlock evaluator). Only 5 paths ship: `python_backend`, `data_sql`, `systems`, `web`, `toolsmith`. |
| Runnable language runtimes | Python, JavaScript, TypeScript, SQL, Lua, Ruby, PHP, R (in-browser runners). Missing as runtimes: C/C++, Rust, Java/C#, Go, Swift/Kotlin, Assembly. HTML/CSS is markup, not a terminal runtime. |
| Language terminal test | Running now on the live IDE: every language the IDE offers, tested for real output. Its results feed directly into §6.1's gaps. |
| Frameworks (§6.2) | Mentioned in the GDD and the 50-level spec. No framework catalog or learning environments built yet. |
| Disciplines (§6.3) | 5 of 13 exist as paths. The other 8 (mobile/desktop, game engines, AI/ML, cybersecurity, embedded, networking/telecom, audio/video/graphics, cloud/DevOps, compilers, OS/distributed, robotics) have no track yet. |
| Shared foundations (§6.4) | Partly covered by existing curriculum topics; no standalone reusable lesson set yet. |
| Project ladders (§6.5) | Missions (23) and topic `projects` fields exist, but the six-rung ladder is not a formal structure yet. |

Build order under "do all" (unchanged): unlock engine → achievements +
rewards → mission formats → authoring + analytics → AI tutor → evidence
endpoints → then Stage 6 content (language catalog + missing runtimes,
framework catalog, the 8 missing disciplines, project ladders). Content
without the engine would be unverifiable, so the engine slices stay first.
