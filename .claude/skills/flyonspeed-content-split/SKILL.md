---
name: flyonspeed-content-split
description: Use when adding, moving, or rewriting content for flyonspeed.org (the Framer front door) or dev.flyonspeed.org (the MkDocs technical manual), launching a new section or page on either site, deciding where a new piece of writing belongs (marketing/credibility vs. install/calibration/protocol), or auditing the GitHub README's pointers between the two sites. Defines what goes where, why, and how to keep the two sites in sync.
---

# FlyONSPEED content allocation policy

You are about to edit one of three surfaces: **flyonspeed.org** (Framer, the front door), **dev.flyonspeed.org** (MkDocs, the technical manual), or the **GitHub README** (the orientation card for builders). This skill tells you which one a piece of content belongs on, where to link from, and what must stay synchronized between them.

When in doubt, the answer is almost always: **front door = credibility, deciding-to-buy, intro-level pilot education; dev = install, configure, calibrate, troubleshoot, protocol, deep theory; GitHub = a one-screen pointer at both.**

## The two-site policy

**flyonspeed.org's job** is to take a pilot who has heard about aural AOA and turn them into a waitlist signup (later: a kit order). It carries the project's credibility (501(c)(3), EAA prizes, team, press), the pilot-facing story (what aural AOA is, why it matters, what it sounds like), the Gen 3 product surface (kit contents, aircraft, avionics, ship target), and recurring contact points (blog, donate, contact). Voice is Vac's — declarative, instructional, deadpan. The reader has not yet committed.

**dev.flyonspeed.org's job** is to take a pilot or builder who has either ordered Gen 3 or is building from source and walk them through every operational step: install, wire, plumb, configure, calibrate, fly, troubleshoot. It also carries the deep technical reference (config parameters, log columns, EFIS protocol bytes, AOA math) and the "how to fly the Navy way" training material. The reader has committed and is now executing. Voice is operating-manual: precise, numbered steps, real units, real bytes, no marketing.

The split exists because the two readers want opposite things. Pilot-deciding wants the story without the wiring colors. Pilot-installing wants the wiring colors and resents the story.

## Audience map

| URL | Who lands here | Mental state | What they want |
|---|---|---|---|
| `flyonspeed.org/` | RV builder, EFIS owner, CFI who heard about it at OSH | "Should I get one?" | Credibility, what it sounds like, ship date, price, team |
| `flyonspeed.org/gen3` | Pilot deciding to commit | "Will it work in my airplane?" | Aircraft list, EFIS list, what's in the kit, what we don't ship |
| `flyonspeed.org/what-is-onspeed` | Pilot still learning the concept | "Why aural AOA?" | Drag curve at pilot level, four references, "fly the airplane to the crash" |
| `flyonspeed.org/team` | Reviewer, journalist, donor | "Are these people credible?" | Bios, EIN, awards, press links |
| `flyonspeed.org/learn` | Pilot who watched one video and wants more | "Where's the playlist?" | Video index, NAFI/WINGS links, white paper PDF |
| `flyonspeed.org/waitlist` | Pilot who already decided | "Sign me up." | The form. Six fields. Done. |
| `dev.flyonspeed.org/installation/*` | Builder with the kit on the bench | "Where does the orange wire go?" | Pin map, mounting clearances, audio panel diagrams |
| `dev.flyonspeed.org/calibration/*` | Pilot post-install, pre-flight | "How do I run the wizard?" | Step-by-step, screenshots, troubleshooting decel runs |
| `dev.flyonspeed.org/efis-integration/*` | Builder mid-install, reading EFIS protocol bytes | "Which Dynon protocol does my SkyView speak?" | Byte counts, baud rates, message IDs |
| `dev.flyonspeed.org/reference/*` | Anyone debugging | "What's `fAlpha0`?" | Parameter table, log columns, glossary |
| `dev.flyonspeed.org/troubleshooting/*` | Pilot whose tones are wrong | "Why is the AOA bar lying?" | Symptom → cause → fix |
| `github.com/flyonspeed/OnSpeed-Gen3` | Developer cloning the repo | "How do I build this?" | `pio run`, test commands, links to both other sites |

If you are about to add content and cannot point to which row of this table it serves, stop and figure that out first. Content that does not serve any specific row of this table does not belong on any of these surfaces.

## Content allocation rules

When you add a new page or section, find the row and follow the rule.

**On flyonspeed.org:**

| Page | What lives there |
|---|---|
| `/team` | Mission, 501(c)(3), EIN, mailing address, EAA prizes (2018 + 2021), award quotes, press links, team bios, project history (Gen 1 → 2 → 3), TronView mention |
| `/what-is-onspeed` | Intro pilot education: what aural AOA is, four references, drag curve, energy management, engine-out narrative, "fly the tone in the pattern." All pitched at the deciding pilot |
| `/gen3` | Kit contents, supported aircraft, supported avionics, what we don't ship, indexer, FAQ (10 questions including price, ship date, certification) |
| `/learn` | Videos, NAFI MentorLIVE, FAA WINGS credit, RV transition syllabus, white paper link, NAFI 2024 paper link |
| `/waitlist` | The form. Six fields. Dev never collects email |
| `/blog` | Project updates, editorial cadence. Dev never has a blog |
| `/partners` (new — see below) | Pilot-facing "what's the ecosystem" — EFIS, audio panels, supporting hardware, training partners |
| `/order` (future — see below) | Pre-orders / kit purchase via Framer Products + Stripe |
| Footer | Donate (PayPal), forum link, GitHub, dev docs link, contact email |

**On dev.flyonspeed.org:**

| Section | What lives there |
|---|---|
| `/installation/*` | Mounting, pneumatics, wiring, audio panel install, pin maps, indexer install |
| `/configuration/*` | First-time setup, web UI, flap setup, sensor calibration |
| `/efis-integration/*` | Dynon `!1`/`!3`, Garmin G3X, MGL, GRT, VectorNav protocol bytes, baud rates, message IDs |
| `/calibration/*` | Wizard walkthrough, decel runs, lift-equation fit. `how-aoa-works.md` carries the math (alpha_0, body angle, percent-lift, NAOA) |
| `/flying/*` (or new `/training/`) | Procedural tone map, normal ops, approach/landing, V-n diagram, **the "Navy way of flying" mind-shift content** (per user) |
| `/reference/*` | Config parameters, log columns, hardware specs, serial protocol, glossary, links to white papers |
| `/troubleshooting/*` | No audio, no EFIS, erratic tones, WiFi, console commands |
| `/software/*` | Building from source, PlatformIO, OTA update |
| `/simulator/*` | X-Plane plugin |

**Future on flyonspeed.org:** `/pilots` and `/cfis` are audience-segmented landing pages that re-rank existing content for those readers. They do not carry new content; they re-order links into the pages above.

**The single sharpest rule:** if you are writing "and the byte at offset 3 is the checksum" on flyonspeed.org, move it. If you are writing "OnSpeed was the 2021 EAA Grand Champion" on dev, delete it — dev's mention of either is one line in `index.md` linking to flyonspeed.org/team.

## Partner systems coverage

Currently scattered: Gen 3 page lists EFIS brands, Learn page references NAFI / FAASafety, team page mentions TronView. There is no single answer to "what does OnSpeed work with?"

**Recommendation: add `/partners` to flyonspeed.org.** Goes on flyonspeed.org. Reason: the deciding pilot wants this in one place; the installing pilot wants protocol bytes (those stay on dev `/efis-integration`). Four sections, no logo grid (the heritage voice does not do logo grids):

- **EFIS systems** — full callouts: Dynon (SkyView, HDX, D10), Garmin (G3X, G3X Touch, G5), MGL. One-line mentions: GRT, VectorNav. Each links to dev's `/efis-integration/<brand>.md` for protocol depth.
- **Audio panels** — PS Engineering, Garmin GMA, Flightcom mentioned on `/gen3` "what avionics" section. No need to duplicate to `/partners` — these are passive integrations.
- **Supporting hardware** — M5Stack (the indexer is M5-based; pilots Google "M5Stack OnSpeed" and should land somewhere). Heated pitots: Dynon and Gretz, surfaced on `/gen3` "what we don't ship."
- **Training partners** — NAFI MentorLIVE, FAASafety.gov, EAA Pilot Proficiency Center, Community Aviation. These are also linked from `/learn`; `/partners` carries one-liners with link-outs.

**Not on `/partners`:** TronView (sibling project, lives in `/team` history); XREAL and Raspberry Pi (TronView's stack, not OnSpeed's); MakerPlane and Vansairforce (footer links and FAQ mentions); Van's Aircraft (Team page; VanGrunsven judged the 2018 prize). These are real relationships but they do not earn a `/partners` card.

## Future store / commerce strategy

Today: a six-field waitlist on Wix, soon migrated to Framer. Tomorrow: pre-orders, then ongoing kit sales.

**Framer + Stripe — what's possible.** Framer Studio supports e-commerce two ways:
1. **Framer's native Store / Products** (CMS-driven product collection, native checkout, Stripe-backed via Framer's payment integration). Reasonable for a single-product catalog (one kit). Limitations: subscription products, complex tax/shipping rules, and inventory sync against an external ERP are weak. We do not need any of that.
2. **Custom Stripe checkout via Framer Code Components** (you write a React component that calls Stripe Checkout). More flexible. More to maintain. Overkill for one product.

**Recommendation: Framer's native Products + Stripe.** Start with one product (the Gen 3 kit), one shipping zone (US), one international rate (rest-of-world flat). Framer Stores handles tax via Stripe Tax. Refund policy lives as a static `/policies` page. This is the minimum viable store and it stays on flyonspeed.org under `/order` — not a subdomain.

**URL: `/order`, not `shop.flyonspeed.org`.** Reason: the deciding pilot's path is `/gen3` → "Targeting AirVenture 2026, $500" → "Order now" CTA → `/order` → checkout. A subdomain breaks that path and forces SEO splitting between two domains. Use a subdomain only if the store grows to a multi-product catalog with its own merchandising voice (T-shirts, training videos, replacement boards). At one-product, `/order` is correct.

**What must be true before flipping the waitlist into pre-orders.** Four conditions, in this order:

1. **BOM final, board cost confirmed.** Until the unit cost is locked you cannot price the kit. Pricing public before this means you eat the difference or apologize publicly. (Owner: Phil + Vac.)
2. **Ship date confirmed within ±2 weeks.** The waitlist promises "AirVenture 2026." A pre-order page promises a date. The FTC's Mail Order Rule requires ship within 30 days unless the page states otherwise — name the date, then ship. (Owner: Vac.)
3. **Stripe account live, Stripe Tax enabled.** Lead time is 2-7 business days. Refund policy + shipping policy + terms of sale drafted and on `/policies`. International shipping rate decided (recommendation: $50 flat, customs paid by buyer). (Owner: Sam.)
4. **Email to the waitlist drafted, scheduled.** Subject: "Pre-orders are open." Body: ship date, price, order link, deadline for first-batch shipping. The waitlist is the conversion mechanism — most pre-orders come from this email, not from organic site traffic. (Owner: Sam.)

When all four are true, do these in order: (a) draft `/order` page on Framer, paint it in heritage voice (no countdown timers, no "limited spots," no "as low as"), (b) wire the Framer Products entry with the single Gen 3 SKU, (c) tighten cross-links — Home Gen 3 callout swaps "Tell us about your aircraft" for "Order now"; the Gen 3 page CTA swaps the same; the waitlist page becomes a fallback ("not ready to commit yet? Get notified for the next run"), (d) send the waitlist email.

**The waitlist does not get deleted at order launch.** It becomes the secondary capture for pilots not yet ready to commit, and the audience for the second production batch.

## Cross-linking rules

The two sites must reach into each other at predictable points. When you add or move content, fix these links.

**flyonspeed.org → dev.flyonspeed.org.** The Gen 3 page links to dev in at least three specific spots: "How it works" section ("the technical version is at dev.flyonspeed.org"), "What you'll need that we don't ship" section ("audio panel configurations" → dev `/installation/audio.md`), and "Calibration" section ("the full install and calibration guide" → dev `/calibration/wizard.md`). The What is OnSpeed page links to dev once: closing paragraph → dev `/calibration/how-aoa-works.md` for the math. The Learn page links to dev `/reference/papers.md` for the white paper. Footer on every page links to dev once as "Technical docs."

**dev.flyonspeed.org → flyonspeed.org.** Dev's `index.md` opens with one sentence linking to flyonspeed.org for project context: "OnSpeed is an open-source aural AOA system for experimental aircraft. For project background, awards, team, and the waitlist, see [flyonspeed.org](https://flyonspeed.org)." Dev's `getting-started/index.md` does the same: a single pointer back to the front door's What is OnSpeed page. Dev's footer links to flyonspeed.org once.

**Dev does NOT host:** team bios, EIN, EAA prize copy, press links, blog, donate button, video library, seminar listings. If you are tempted to add any of these to dev, link to flyonspeed.org instead.

**flyonspeed.org does NOT host:** pin maps, EFIS protocol byte counts, console command tables, log column reference, calibration wizard step-by-step. If tempted, link to dev.

**GitHub README orientation.** The README is the developer's first hit. It must contain, in this order: (1) one-line description, (2) link to flyonspeed.org for "what is this and who's behind it," (3) link to dev.flyonspeed.org for "how to install / configure / calibrate," (4) build/test commands for `pio run` and `pio test`, (5) license (GPLv3) and contact. No project history, no team, no awards on the README — link out.

## "Where does this go?" decision tree

When you are about to add content, walk this top-to-bottom. The first row that matches wins.

1. **Is it a number, a parameter name, a wire color, a baud rate, a console command, a config XML element, or a code snippet?** → dev. No exceptions.
2. **Is it a step-by-step procedure for someone who already has the hardware in hand (install, calibrate, configure, troubleshoot, OTA update, build from source)?** → dev.
3. **Is it the deep math (alpha_0, lift equation fit, body-angle vs. wing-AOA distinction, EKF6 internals)?** → dev `/calibration/how-aoa-works.md` or `/reference/`.
4. **Is it the "Navy way of flying" mind-shift, the energy-management mental model, or "fly the tone for engine-out"?** → If pitched at the deciding pilot (story version): flyonspeed.org `/what-is-onspeed`. If pitched at the committed pilot operationally (training version, with calibration cross-links): dev `/flying/`.
5. **Is it about the project itself — who built it, awards, press, mission, donations, blog?** → flyonspeed.org. Never dev.
6. **Is it a video, a seminar listing, a WINGS event, or a training resource PDF?** → flyonspeed.org `/learn`. Never embed videos on dev.
7. **Is it a fact a pilot needs to decide whether to commit (price, ship date, what aircraft it works with, what's in the kit, what's not in the kit, what EFIS it supports)?** → flyonspeed.org `/gen3`.
8. **Is it a partner integration callout (Dynon, Garmin, MGL, M5Stack)?** → flyonspeed.org `/partners` (when it exists; for now `/gen3`). Protocol depth always to dev.
9. **Is it a form, an order, a payment, an email collection, or a CTA?** → flyonspeed.org. Dev never collects email.
10. **Are you writing in first-person plural ("we built…", "we shipped…")?** → flyonspeed.org. Dev voice is third-person operating-manual.

If you walked through all ten and nothing matched, the content probably does not belong on either site. Push back to the user before publishing anywhere.

## What's currently in the wrong place

Findings from the current state. Fix these as you encounter them.

1. **Issue #110 plans most of its proposed Phase 2 content for the dev docs site. Most of it should land on flyonspeed.org instead.** Specifically: §A (Credibility & Organization), §B (Video Library), §C (Seminars & Training), §D items 8-10 (Pilot Education at intro level), §E (Project History & Ecosystem — except where it overlaps with `/software/`), §F (Community & External Links — except technical papers section). All belong on flyonspeed.org. Close those subsections of #110 once the Framer pages ship; do not migrate them to dev.

2. **Dev's `getting-started/what-is-onspeed.md` overlaps the front-door `/what-is-onspeed` page.** The dev version is well-written but it is *almost identical content* to the front-door draft. Resolution: dev's getting-started becomes a one-paragraph welcome that points outward — "if you are deciding whether OnSpeed is right for your aircraft, read [flyonspeed.org/what-is-onspeed](https://flyonspeed.org/what-is-onspeed). If you have a kit in hand, start at [Installation](../installation/index.md)." The front-door page becomes the canonical pilot education. Do not maintain both versions; they will drift.

3. **Dev's `getting-started/system-overview.md`, `what-you-need.md`, `safety.md` partly duplicate the Gen 3 page on flyonspeed.org.** Audit: anything that is "what's in the kit" or "what aircraft does it work with" should be on `/gen3` only. Dev's getting-started should describe the *system as installed*, not the *kit as sold*. Different reader, different content.

4. **The `/gen3` FAQ entry "Will you offer installation services?" duplicates dev's troubleshooting guidance.** Pick one. Recommendation: keep it on `/gen3` (deciding pilot needs the answer); remove from dev troubleshooting if it is there.

5. **The Learn page is currently in two places conceptually.** Issue #110 plans a `learn/` section on dev. The Framer site already has `/learn` drafted. Do not build dev's `learn/`. Close that nav section in `mkdocs.yml` if it ever appears.

6. **The "Navy way of flying" mind-shift content is currently nowhere.** Per user, this is dev-side training content. Add it to dev under `/flying/` (or carve out `/training/` if it grows past 2-3 pages). Cross-link from `/calibration/how-aoa-works.md`. Do not add it to flyonspeed.org `/what-is-onspeed` — that page already does the gentler intro version.

7. **GitHub README has not been audited for the split.** If it currently includes team bios, awards, or links to flyonspeed.org/team, those are fine. If it includes the install procedure inline rather than linking to dev, that is wrong — link out.

8. **Asset duplication on Framer.** All ONSPEED 101 / energy management diagrams listed in `asset-inventory.md` belong on `/what-is-onspeed`. The `vn-envelope-*`, `aural-aoa-logic`, `push-pull-model` diagrams should not also appear on dev's `/calibration/` or `/flying/` pages — dev uses its own diagrams (KaTeX equations, schematic-style figures), not the marketing illustrations. If dev needs a V-n diagram, it draws one in code.

## Sync hygiene

The two sites must agree on these facts. When any of these change, update **both sites in the same PR**. Treat divergence as a bug.

| Fact | Current value | Where it lives |
|---|---|---|
| Gen 3 ship target | "AirVenture 2026" | flyonspeed.org `/gen3`, `/waitlist`, home; dev `/index.md` if mentioned |
| Gen 3 retail target | $500 | flyonspeed.org `/gen3` only — dev does not quote price |
| Aircraft compatibility list | RV-3/4/6/7/8/9/10/14, Harmon Rocket, Sling LSA, Sling 4 TSI | flyonspeed.org `/gen3` and `/partners` (if added) |
| Avionics compatibility list | Dynon SkyView/HDX, Garmin G3X/G5, MGL, GRT, VectorNav, standalone | flyonspeed.org `/gen3` and `/partners`; dev `/efis-integration/index.md` |
| Mailing address | 1202 Windward Circle, Niceville, FL 32578 | flyonspeed.org `/team` and footer |
| EIN | 83-2140220 | flyonspeed.org `/team` and footer |
| Florida document number | N18000010659 | flyonspeed.org `/team` |
| Contact email | team@flyonspeed.org | Both sites' footers; dev `/index.md`; GitHub README |
| GitHub repo | github.com/flyonspeed/OnSpeed-Gen3 | Both sites' footers; flyonspeed.org `/gen3` open-source section |
| Older repos | github.com/flyonspeed/OnSpeed-Gen2 | flyonspeed.org `/team` history section |
| Donate URL | PayPal hosted-button (real ID still TBD) | flyonspeed.org `/team`, footer |
| 501(c)(3) language | "Florida 501(c)(3) public charity. Donations are tax-deductible." | flyonspeed.org `/team`, footer, `/waitlist` auto-reply |
| Voice attribution | "Mike 'Vac' Vaccaro" not "Mike Vaccaro" | All bios, all attributed quotes |
| Dev site URL | dev.flyonspeed.org | flyonspeed.org footer + Gen 3 cross-links; GitHub README |
| Front-door URL | flyonspeed.org | dev footer + index pointer; GitHub README |

When you change any of these on one site, grep the other site (and the GitHub README) for the old value and update in the same PR. CLAUDE.md's "PR style" note applies: keep these changes deadpan and present-tense.

## What this skill does NOT cover

- **Visual layout, Framer XML gotchas, breakpoints, color tokens, font selectors** — see `framer-mcp` skill. This skill says *what* to write; that one says *how to get it into Framer without breaking it*.
- **Voice, tone, banned phrasings, present-tense PR copy** — see `/Users/sritchie/code/onspeed/CLAUDE.md`. This skill says *where content lives*; that file says *how it sounds*.
- **Plan-level decisions about scope, timing, what to ship for Oshkosh, cutover sequencing** — see `/Users/sritchie/code/onspeed/local-plans/PLAN_FLYONSPEED_OSHKOSH.md`. This skill assumes the plan; it does not negotiate it.
- **The math itself (alpha_0, body-angle, percent-lift formula)** — see CLAUDE.md's "OnSpeed measures body angle, not wing AOA" section, plus dev's `/calibration/how-aoa-works.md`. This skill says *the math goes on dev*; it does not derive it.
- **Dev docs accuracy against firmware (config parameters, console commands, log columns)** — see `docs-update` skill. Run that before each release.
- **Permissions, hooks, settings.json** — see `update-config` skill.

If a question is about *where* content lives or how the two sites talk to each other, answer it from this skill. If it is about *what to write* or *how to render it*, defer to the skills above.
