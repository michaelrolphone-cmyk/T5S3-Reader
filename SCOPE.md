# Project Vision & Scope: CrossPoint Reader

> **RiscRTE scope note:** This document preserves the inherited scope of the **CrossPoint Reader** subsystem and its reader-specific design goals. It is not the scope of the overall RiscRTE firmware/runtime. RiscRTE is a general embedded runtime and intentionally supports applications, services, drivers, and platform capabilities that are outside this reader-only scope. CrossPoint should be used here only for the ebook-reader subsystem and inherited reader behavior.

The goal of CrossPoint Reader is to create an efficient, open-source reading experience for the Xteink X4. We believe a
dedicated e-reader should do one thing exceptionally well: **facilitate focused reading.**

## 1. Core Mission

To provide a lightweight, high-performance reader that prioritizes legibility and
usability over "swiss-army-knife" functionality.

## 2. Scope

### In-Scope

*These are features that directly improve the primary purpose of the reader subsystem.*

* **User Experience:** E.g. User-friendly interfaces, and interactions, both inside the reader and navigating the
  reader experience. This includes things like button mapping, book loading, and book navigation like bookmarks.
* **Document Rendering:** E.g. Support for rendering documents (primarily EPUB) and improvements to the rendering
  engine.
* **Format Optimization:** E.g. Efficiently parsing EPUB (CSS/Images) and other documents within the device's
  capabilities.
* **Typography & Legibility:** E.g. Custom font support, hyphenation engines, and adjustable line spacing.
* **E-Ink Driver Refinement:** E.g. Reducing full-screen flashes (ghosting management) and improving general rendering.
* **Library Management:** E.g. Simple, intuitive ways to organize and navigate a collection of books.
* **Local Transfer:** E.g. Simple, "pull" based book loading via a basic web-server or public and widely-used standards.
* **Language Support:** E.g. Support for multiple languages both in the reader and in the interfaces.
* **Reference Tools:** E.g. Local dictionary lookup. Providing quick, offline definitions to enhance comprehension 
  without breaking focus.
* **Clock Display (device dependent):** 

| Device | Scope |
| -- | -- |
| X3 | The X3 uses a dedicated DS3231 RTC, which maintains accurate time across sleep cycles and can be treated as a reliable wall clock. |
| X4 | The X4 relies on the ESP32-C3's internal RTC, which drifts significantly during deep sleep. NTP sync could correct this, with an appropriate user experience around connecting to the internet on wake or on demand. This causes some tension with the **Active Connectivity** section below, so please open a discussion about this UX if it's a feature you would find useful. |

### Out-of-Scope for CrossPoint Reader

*These items are outside the reader subsystem's focused mission. They are not necessarily out of scope for RiscRTE applications or platform capabilities.*

* **Interactive Apps:** No Notepads, Calculators, or Games inside the CrossPoint Reader subsystem.
* **Active Connectivity:** No RSS readers, News aggregators, or Web browsers as reader features. Background connectivity belongs to RiscRTE platform/services where applicable.
* **Media Playback:** No Audio players or Audio-books as CrossPoint Reader features.
* **Complex Annotation:** No typed out notes as part of the focused reader experience.

### In-scope — Technically Unsupported

*These features align with CrossPoint Reader's goals but are impractical on the legacy reader hardware or produce poor UX.*

* **PDF Rendering:** PDFs are fixed-layout documents, so rendering them requires displaying pages as images rather than reflowable text — resulting in constant panning and zooming that makes for a poor reading experience on e-ink.

## 3. Idea Evaluation

When evaluating changes specifically to CrossPoint Reader, prioritize a lightweight, reliable, and performant reading experience. Platform-wide RiscRTE features should instead be evaluated against the RiscRTE architecture specifications and capability roadmap.

> **Note to Contributors:** For reader-specific changes, use this document together with the RiscRTE master specification. For platform-wide changes, `docs/RISCRTE_PLATFORM_SPEC.md` and `docs/PLATFORM_CAPABILITY_ROADMAP.md` are authoritative.
