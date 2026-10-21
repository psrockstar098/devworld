# DEVWORLD — Lifestyle, Work, Social Dynamics & Reputation Engine

**Version:** 0.1 (2026-10-21)  
**Reading guide:** [PRD] `01-product-requirements.md` defines the human-first philosophy; [GDD] `02-game-design-document.md` defines §1 (Identity & Reputation), §5 (Travel), §6 (Housing), §7 (Jobs & Companies), and §8 (Social Spaces); [08] is the education system (evidence-based mastery); [09] is the progression engine (separate capability records). This document formalizes the five core lifestyle, social, and professional pillars:
1. **Freedom to Rest** — Anti-burnout architecture, offline progress, and flexible pacing.
2. **Meaningful Life Events** — Capability-driven milestones (jobs, research, competitions, mentoring, investors).
3. **Emergent Friendships** — Natural affinity, collaborative problem solving, and long-term bonds.
4. **Evidence-Based Reputation & Trust** — Verifiable reliability, honest communication, and peer memory.
5. **Realistic Commuting & Work Choices** — Strategic trade-offs between home, office, coworking, and lab.

---

## §1 — Purpose & Core Philosophy

DEVWORLD rejects predatory engagement loops:
- **No Daily Chores or Streak Anxiety:** Learning programming requires cognitive space. Players are never penalized, degraded, or locked out of content for prioritizing real life, family, or sleep.
- **Evidence Over Popularity:** Reputation is not a vanity metric, follower count, or charisma contest. Trust is an objective track record of delivering working code, communicating honestly, and supporting peers.
- **Life as a Software Craftsman:** Career progression is not a linear ladder of grinding generic tasks. It is an emergent narrative of technical milestones, organic professional invitations, meaningful relationships, and deliberate daily workspace choices.

```
                          ┌────────────────────────┐
                          │   Freedom to Rest      │
                          │ • Offline Async Engine │
                          │ • Zero Streak Penalty  │
                          │ • Soft Deadlines       │
                          └───────────┬────────────┘
                                      │ enables
                                      ▼
┌────────────────────────┐  informs   ┌────────────────────────┐  rewards   ┌────────────────────────┐
│ Commute & Work Spaces  │───────────▶│  Player Capability &   │───────────▶│  Meaningful Life       │
│ • Home / Remote        │            │  Evidence Ledger       │            │  Events                │
│ • Coworking Loft       │            │ • Verified Passes      │            │ • Job Offers           │
│ • Dedicated Office     │            │ • Test Rigor           │            │ • Research Invitations │
│ • Research Lab         │            │ • Spaced Review        │            │ • Angel Investments    │
└────────────────────────┘            └───────────┬────────────┘            └────────────────────────┘
                                                  │
                                                  ▼
                                      ┌────────────────────────┐
                                      │  Trust & Social Graph  │
                                      │ • Evidence Reputation  │
                                      │ • Natural Friendships  │
                                      │ • Collaborative Memory │
                                      └────────────────────────┘
```

---

## §2 — Pillar 1: Freedom to Rest (Anti-Burnout & Offline Engine)

### 2.1 The Anti-Burnout Invariant
1. **Zero Daily Streak Attrition:** There are no login streaks, daily check-in bonuses, or perishable buffs that decay if the player does not connect every 24 hours.
2. **Offline Spaced Review Protection:** Memory decay models ([08] §5) freeze during extended absences. If a player steps away for three weeks, their spaced review intervals do not collapse into an overwhelming mountain of overdue reviews; reviews resume with a graduated refresher sequence.
3. **Flexible Deadlines & Pause States:** Contracts and jobs possess soft target windows rather than punitive timers. If real-world emergencies arise, players can toggle **Pause / Personal Leave** without loss of reputation.

### 2.2 Offline Progress & Background Systems
While the player is offline, the game world continues to simulate realistic background tasks that respect physical time without demanding active presence:
- **Asynchronous Test Suites & CI Runs:** Lengthy integration runs, regression sweeps, and property-based test suites execute on sandbox workers without requiring the game client to stay open.
- **Long-Running Simulations & Model Training:** Machine learning convergence runs, cluster simulations, or large-scale data imports advance based on wall-clock elapsed time up to a configurable cap (e.g., 7 days).
- **Passive Software Revenue & Library Adoption:** Software published by the player ([GDD] §10) continues to be fetched by in-world package managers, yielding micro-royalties and community usage telemetry stored in the double-entry ledger.
- **Home Lab Background Maintenance:** Automated backup routines, scheduled database vacuuming, and server health checks placed in the player's home or studio generate maintenance reliability evidence.

### 2.3 The "Well-Rested" Cognitive State
Returning after a period of rest confers the **Well-Rested** status:
- Rest does not award free levels or unearned skill unlocks.
- Instead, it grants a **Cognitive Clarity** bonus: +25% XP efficiency on delayed-recall reviews and reduced error fatigue, reflecting a refreshed human mind.

---

## §3 — Pillar 2: Meaningful Life Events

### 3.1 Capability-Driven Milestone Triggers
Life events are never randomized loot drops or arbitrary time-gated interruptions. They trigger deterministically when a player's `PlayerCapability` snapshot meets clear, verifiable thresholds across mastery, code quality, and domain focus.

```
       [ Capability Snapshot ]
  Level • Mastery • Language Tier • Reliability
                 │
                 ▼
       [ Event Qualification ]
                 │
   ┌─────────────┼─────────────┬─────────────┐
   ▼             ▼             ▼             ▼
Job Offer   Research Team   Hackathon    Mentorship   Investor Approach
 (Tech Co)   (Ada / Campus) (Open Cup)   (Junie / P2P) (Nova / Seed Fund)
```

### 3.2 The Five Canonical Life Events

| Life Event | Triggering Capabilities | Key World Actor | The Opportunity & Consequence |
|---|---|---|---|
| **Industry Job Offer** | Reliability Tier $\ge$ `trusted`, Language Proficiency Tier $\ge$ 3 in demanded stack, $\ge$ 5 clean contracts. | Tech Firm Hiring Managers (e.g., Nexus Data, Foundry Systems). | Transition from sporadic gig contracts to salaried employment with health benefits, structured team repos, dedicated office space, and stock options. |
| **Research Team Invitation** | Completed $\ge$ 2 Independent Capstone Gates, high Transfer evidence score ([08]), advanced algorithms mastered. | Dr. Ada (Innovation Capital Research Director). | Invitation to join a cutting-edge laboratory exploring distributed consensus, compilers, or neural architectures. Unlocks restricted academic libraries and specialized hardware benches. |
| **Competition & Hackathon** | Reached Regional Milestone or Community Standing; open registration window. | City Tech Councils & Global Guilds. | 48-hour asynchronous programming tournament with real-time test verification, architectural judging, cash grants, and high-visibility portfolio awards. |
| **Mentorship Request** | Flawless/Trusted reliability, mastered fundamental topics, demonstrated positive peer code reviews. | Junie (Mission Guide) or junior community players. | Request to guide novice developers through difficult hurdles. Earns Mentorship Evidence, deepens social bonds, and accelerates the mentor's own delayed-recall mastery. |
| **Investor Approach** | Player has founded a company (`CompanyState`), released a functional software package, and achieved positive revenue/adoption. | Nova (Startup City Founder & Angel Network). | Seed funding or Series A term sheet offering venture capital in exchange for milestone delivery, enabling office leases, hiring NPC/player engineers, and scaling infrastructure. |

### 3.3 Event Lifecycle & Non-Expiring Opportunities
Events arrive via the player's terminal/mailbox as formal proposals. Because DEVWORLD respects player autonomy:
1. **Deliberation Window:** Life event invitations never evaporate within hours. They persist until explicitly accepted, declined, or countered.
2. **Transparent Terms:** Every offer displays prerequisites, expectations, compensation, IP rights, and time commitments upfront.
3. **Archival to Portfolio:** Accepted and completed milestones generate immutable cryptographic evidence entries in the player's public `Proof of Skill` record.

---

## §4 — Pillar 3: Friendships That Develop Naturally

### 4.1 Organic Social Affinity
Relationships in DEVWORLD cannot be bought with gifts, superficial emote spam, or flattery. Bonds develop through shared struggle and technical alignment.

```
   Shared Technical Interests (Common Curriculum Tags & Tools)
                              +
   Crisis Recovery & Debugging Together (Diff Support, Test Rescues)
                              +
   Repeated Multi-Author Collaboration (Shipped PRs, Co-Founding)
                              +
   Community Engagement (Tech Talks, District Hackathons)
                              ▼
                  [ Natural Friendship Level ]
       Acquaintance ──▶ Collaborator ──▶ Trusted Peer ──▶ Lifelong Ally
```

### 4.2 Interaction Vectors & Memory
NPCs and other players maintain an interaction log that tracks:
- **Collaborative Problem Solving:** Pairing on an arduous mission or resolving a failing test suite creates a permanent bond weight.
- **Code Review Empathy:** Providing thorough, constructive, and respectful review comments increases rapport; rubber-stamping or caustic rejection damages trust.
- **Shared Mission Vectors:** Developers who frequently frequent the same tech districts, libraries, or dungeon repositories naturally build familiarity.

### 4.3 Relationship Progression Tiers
1. **Stranger:** Baseline neutral interaction. Standard commercial transactions and public mission board interactions.
2. **Acquaintance:** Recognized in local districts. Unlocks personalized greetings, lore hints, and invitations to open study groups.
3. **Collaborator:** Proven co-worker. Unlocks co-working invites, shared private git branches, and joint contract bids.
4. **Trusted Peer:** High mutual evidence score. Unlocks private research lab access, co-founding company agreements, personal code recommendations, and mutual project references.
5. **Lifelong Ally:** Deep, verified history of overcoming difficult technical milestones together. Unlocks permanent mentorship networks, equity partnership opportunities, and legacy project stewardship.

---

## §5 — Pillar 4: Personal Reputation and Trust

### 5.1 Evidence-Based Trust vs. Popularity Contests
In commercial platforms, reputation is frequently corrupted by popularity metrics: follower counts, astroturfed upvotes, or charismatic self-promotion. DEVWORLD mandates that **Reputation is an Evidence-Derived Tensor**.

A player's reputation is evaluated across four distinct axes:
$$\mathbf{R} = \langle R_{\text{delivery}}, R_{\text{accuracy}}, R_{\text{teamwork}}, R_{\text{craftsmanship}} \rangle$$

```
              [ Craftsmanship ]
              (Test pass ratio,
               code clean score)
                      ▲
                      │
  [ Accuracy ] ◄──────┼──────► [ Teamwork ]
  (Honest estimates,  │        (PR reviews, unblocking
   no fake passes)    │         peers, mentoring)
                      ▼
               [ Delivery ]
             (On-time shipping,
              low abandon rate)
```

### 5.2 The Reputation Dimensions

1. **Delivery Reliability ($R_{\text{delivery}}$):**
   - Computed from `ReliabilityRecord`: ratio of completed contracts to abandoned contracts.
   - Graceful pause states do not count as abandonments.
2. **Communication Accuracy ($R_{\text{accuracy}}$):**
   - Measured by the alignment between a developer's estimated completion time/scope and actual deliverables.
   - Transparency when reporting unexpected bugs or security regressions.
3. **Teamwork & Altruism ($R_{\text{teamwork}}$):**
   - Measured by unblocking peers in team missions, constructive code reviews in team repositories, and mentorship contributions.
4. **Code Craftsmanship ($R_{\text{craftsmanship}}$):**
   - Derived from edge-case test suite performance, absence of regressions in shared codebases, and adherence to clean architectural guidelines.

### 5.3 Asymmetric Reputation Mechanics
- **Slow Accrual:** High trust is built gradually over multiple successfully verified projects, clean PR merges, and reliable deliveries.
- **Acute Accountability:** Severe breaches (e.g., committing malicious exploits, breaking NDA contracts, abandoning client systems during critical outages) result in steep, evidenced reputation penalties that require earnest restitution work to rehabilitate.
- **Transparent Justification:** Any party inspecting a player's reputation score can inspect the underlying evidence log: *"98% on-time delivery across 24 contracts; 0 critical regressions; 14 peer reviews completed."*

---

## §6 — Pillar 5: Realistic Commuting & Work Choices

### 6.1 The Four Workspace Environments
Players can choose where and how they conduct their development work. Each option introduces distinct trade-offs between living expenses, transit time, isolation, and collaboration synergies.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Workspace Selection                             │
├──────────────────┬──────────────────┬──────────────────┬───────────────┤
│ 1. Home / Remote │ 2. Coworking     │ 3. Dedicated     │ 4. Specialized│
│    Apartment     │    Loft          │    Office        │    Lab/Studio │
│                  │                  │                  │               │
│ • Zero Commute   │ • Short Walk     │ • Transit Bus    │ • High-Tech   │
│ • Zero Desk Fee  │ • Daily/Mo Rent  │ • Free Desk      │ • Costly Rent │
│ • High Comfort   │ • Networking     │ • Team Synergies │ • Ultra Fast  │
│ • No Social Walk │ • Peer Encounters│ • Rigid Schedule │ • Deep R&D    │
└──────────────────┴──────────────────┴──────────────────┴───────────────┘
```

### 6.2 Workspace Matrix

| Workspace | Location Map | Commute Overhead | Direct Financial Cost | Social & Collaboration Bonus | Environmental Perks |
|---|---|---|---|---|---|
| **Home Apartment** | `apt_interior` | 0 mins (instant step to workstation) | 0 credits (covered by base apartment upkeep) | Isolated: zero serendipitous peer encounters; cannot host large team scrums. | Fully customizable layout; trophy displays; maximum personal comfort. |
| **Coworking Loft** | `startup_cowork` | Low (walk from residential quarters) | Moderate daily or monthly desk subscription | High Serendipity: encounter freelance NPCs and remote players; spontaneous contract leads. | Shared high-speed compiler racks; commercial espresso bar (+energy boost); meeting booths. |
| **Corporate Office** | `startup_office` / Enterprise Towers | High (requires regional bus transit / walking across districts) | Zero direct fee (subsidized by employer) | High Focus Teamwork: direct access to company teammates; pairing bonus on company repos. | Access to internal staging clusters; corporate mentor access; company equipment allowances. |
| **Specialized Lab / Foundry** | Systems District / Innovation Foundry | Variable (district transit) | High institutional lease fee or research grant | Focused Scientific: access to specialized hardware researchers and domain specialists. | Access to hardware emulation rigs, bare-metal debuggers, and high-concurrency clusters. |

### 6.3 Commuting Mechanics & Regional Transit
- **Physical Transit vs. Fast Travel:** Players can physically walk between districts or board city transit buses.
- **Travel Time as Strategic Resource:** Commuting consumes in-game minutes. Living closer to work or securing remote contracts saves time for learning and building.
- **Commuter Serendipity:** Riding public transit or walking urban pedestrian boulevards triggers realistic ambient events: encountering colleagues, overhearing tech rumors, or reading public job bulletin boards.
- **Work-From-Anywhere Laptops:** Portable computers allow light coding (bug triage, reading documentation, script editing) anywhere in the world (parks, transit, cafes), while heavy compilation and multi-tier test suites benefit from fixed desktop workstations and server racks.

---

## §7 — Data Model & Systems Integration

### 7.1 C++ Data Structures (`game-shell/src/types.h`)

```cpp
namespace dw {

// Pillar 1: Freedom to rest & offline simulation
struct RestState {
  int64_t lastLogoutMs = 0;
  int64_t accumulatedRestSec = 0;
  bool isPaused = false;
  std::string pauseReason;
  int wellRestedBonusPercent = 0;  // Applied to review efficiency
};

// Pillar 2: Meaningful life events
enum class LifeEventKind {
  JobOffer,
  ResearchInvitation,
  CompetitionInvite,
  MentorshipRequest,
  InvestorApproach
};

struct LifeEventDef {
  std::string id;
  LifeEventKind kind;
  std::string title;
  std::string organization;
  std::string senderNpcId;
  std::string proposalText;
  std::map<std::string, int> capabilityRequirements;
  int64_t compensationCredits = 0;
  int64_t equityPercent = 0;
  int64_t receivedAtMs = 0;
  bool accepted = false;
  bool dismissed = false;
};

// Pillar 3: Emergent social relationships
enum class RelationshipTier {
  Stranger,
  Acquaintance,
  Collaborator,
  TrustedPeer,
  LifelongAlly
};

struct SocialBond {
  std::string contactId;
  RelationshipTier tier = RelationshipTier::Stranger;
  int affinityPoints = 0;
  int collaborativeMissionsPassed = 0;
  int codeReviewsExchanged = 0;
  int timesUnblocked = 0;
  int64_t lastInteractedMs = 0;
};

// Pillar 4: Evidence-based reputation tensor
struct TrustTensor {
  int deliveryScore = 0;       // 0..100 (Pass/abandon ratio)
  int accuracyScore = 0;       // 0..100 (Honest estimates, clean reports)
  int teamworkScore = 0;       // 0..100 (Assistance, peer reviews)
  int craftsmanshipScore = 0;  // 0..100 (Code quality, test coverage)

  int compositeReputation() const {
    return (deliveryScore * 30 + accuracyScore * 20 + teamworkScore * 25 + craftsmanshipScore * 25) / 100;
  }
};

// Pillar 5: Workspaces & Commute
enum class WorkspaceType {
  HomeApartment,
  CoworkingLoft,
  CorporateOffice,
  SpecializedLab
};

struct WorkspaceChoice {
  WorkspaceType currentType = WorkspaceType::HomeApartment;
  std::string designatedDeskId;
  std::string targetMapId = "apt_interior";
  int64_t dailyLeaseCost = 0;
  int buildSpeedMultiplierPercent = 100;
  int collaborationBonusPercent = 0;
  int commuteMinutes = 0;
};

}  // namespace dw
```

### 7.2 Storage Architecture & Event Flows
- **ClickHouse:** System of record for all append-only event streams:
  - `life_event_records(id, player_id, event_kind, organization, status, timestamp)`
  - `social_collaborations(id, player_a, player_b, mission_id, test_pass_count, timestamp)`
  - `commute_trips(id, player_id, origin_map, dest_map, transit_type, fare, duration)`
- **RocksDB / Kvrocks:** Durable current state for active player workspaces, active employment contracts, current relationship levels, and pending life event offers.
- **Redis:** Transient presence, active coworking desk occupancy, real-time bus arrival schedules, and live notifications.

---

## §8 — Verification & Testing Strategy

1. **Anti-Decay Invariant Tests:** Verify that jumping system clocks forward by 180 days does not degrade player levels, invalidate mastered curriculum skills, or trigger punitive status conditions.
2. **Determinism in Life Events:** Given identical capability records and portfolio histories, the life event evaluator must emit identical qualification sets without jitter or randomness.
3. **Reputation Math Unit Tests:**
   - Proving that unproven players begin at neutral baseline.
   - Proving that failing/abandoning contracts impacts $R_{\text{delivery}}$ while honest early cancellation yields lower penalty than silent abandonment.
   - Proving that cosmetic ownership, equipment tier, and financial wealth have mathematical weight $0.0$ in reputation calculations.
4. **Commute & Workspace Matrix Tests:** Validate that leasing a coworking desk correctly debits daily upkeep via the double-entry ledger, applies the collaboration coefficient to paired missions, and adjusts commute transit costs accurately across district boundaries.
