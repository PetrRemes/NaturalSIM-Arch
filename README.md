[🇬🇧 English](README.md) | [🇨🇿 Čeština](README_CS.md)

# NaturalSIM — Architecture

> A procedural world simulation in Unreal Engine 5, focused on the emergent behavior of nature and civilization.

NaturalSIM is an experimental simulation of an interconnected world where natural, climatic, and human systems interact with each other.

The goal is not to create a world built on pre-scripted scenarios, but a system where long-term changes emerge from the interaction of individual subsystems.

The climate affects the environment, the environment influences flora and fauna, resource availability impacts humans, and human activity subsequently shapes the world around them.

---

### 🎬 Project and Architecture Video Showcase
*For a quick overview of the project's logic, optimization approaches, and to see a running prototype, I recommend watching this short introduction (Note: Audio is in Czech, please turn on YouTube Auto-translate Subtitles):*

[![NaturalSim - Představení](https://img.youtube.com/vi/hlnHsbXkwEY/maxresdefault.jpg)](https://www.youtube.com/watch?v=hlnHsbXkwEY)

---

## Core Architecture

<img width="1536" height="1024" alt="Schéma systémů" src="https://github.com/user-attachments/assets/3c486f82-57c5-486c-9ecc-1e712004f90c" />

The foundation of the project is a modular architecture where individual systems operate independently while passing data required for the global simulation.

The central **World Manager** oversees the world and coordinates individual parts of the simulation.

The **Simulation Director** distributes calculations over time based on the simulation's current needs, ensuring heavy operations do not overlap in the same tick and cause unnecessary performance drops (freezes).

The world is divided into a **Chunk System**, which allows for localized processing and activity management.

Individual systems utilize a shared data layer consisting of world cells. This allows them to read and write environmental data without the need for hard references and direct coupling between each system.

---

## Key Subsystems

- **Climate & Hydro System:** Global climate, atmospheric conditions, precipitation, surface and groundwater
- **Tectonic & World Generator:** Procedural terrain generation, plate tectonics, and geological changes
- **Flora & Fauna System:** Ecosystems, biomes, vegetation growth, and population dynamics
- **Human System:** Population, migration, sustenance, environmental response, and knowledge progression
- **Settlement System:** Settlements, buildings, infrastructure, economy, growth, and expansion
- **Civilization System:** Nations, culture, technology, diplomacy, and the long-term evolution of civilizations
- **Transport System:** Roads, ships, trade routes, and settlement connections
- **Disaster System:** Natural disasters and their impact on both natural and human systems
- **History System:** Recording of significant events, changes, and the long-term history of the world

---

## World Data Layer

One of the main architectural principles is the decoupling of systems using the world data layer.

Each cell contains information about its current state, allowing systems to independently read the data they need for their own calculations.

For instance, the Flora System does not need to communicate directly with the Climate System.

Instead, it simply reads the temperature, humidity, water, and other environmental properties directly from the corresponding cell.

This principle allows individual subsystems to remain relatively independent while still forming a fully interconnected world.

---

## Simulation and Visualization

The simulation logic is strictly separated from the visualization layer.

The **Simulation** primarily handles data and the internal state of the world.

The **Visualization** translates this data into the final representation of terrain, water, vegetation, fauna, people, settlements, and other elements.

Thanks to this separation, the complete visual representation of the world doesn't need to be continuously recalculated just because the internal simulation state changed.

The world can continue its calculations entirely independently of what the camera is currently rendering.

---

## Performance and Optimization

During development, I encountered an issue where multiple heavy calculations converged in the same tick, causing regular performance spikes (freezes).

The solution was to divide calculations into separate time slices and create a custom management system to handle them.

### Simulation Director

The Simulation Director monitors the current needs of the simulation and distributes the workload across individual tick slices.

The goal is to prevent all systems from executing demanding operations simultaneously, creating a much smoother distribution of calculations over time.

The project also utilizes principles such as:

- Asynchronous / background calculations
- Chunk-based simulation
- LOD (Level of Detail)
- HISM / Instancing
- Streaming
- Profiling & Telemetry

---

## Emergent Behavior

NaturalSIM is not based on pre-scripted events.

Individual systems follow their own rules, and their mutual interactions can create long-term consequences that are never explicitly defined as standalone scenarios.

For example, a change in the environment can affect food availability, which alters population movement, leading to the establishment of a new settlement, and subsequently, a different trajectory for civilizational development.

The purpose is to create a world where systems are not isolated, but collectively author history.

---

## Current State

NaturalSIM is currently an active prototype.

The core simulation engine and several individual systems are already functional and interconnected. Other simulation components and visual presentations are still under development.

The simulation has also been tested at significantly accelerated speeds and managed to run continuously for over **1,000 simulated years without crashing**.

---

## My Role

On this project, I focus primarily on:

- Simulation architecture design
- Designing individual systems and their interdependencies
- Game and simulation logic
- Data structures and information flow
- Designing optimization solutions
- Prototyping and integration
- Testing and debugging
- Visual presentation of the simulation

During implementation, I heavily utilize **AI as a tool for code generation and refactoring**.

I am not a traditional C++ programmer. My main role lies in designing the logic and architecture, deciding how individual parts of the system should collaborate, integrating the resulting code, testing, and iterating on the project.

---

## Project Principle

> **No fixed script. A living world.**

NaturalSIM is an experiment asking the question: what happens if we give individual systems their own rules, data, and interactions, and let them influence each other over a long period of time?

Instead of a pre-written story, it creates a space where narrative emerges naturally from the evolution of the world itself.

---

## Copyright & License

© 2026 Petr Remeš. Všechna práva vyhrazena / All rights reserved.

This repository serves strictly as a personal portfolio and architectural showcase. The source code, structure, visual assets, and game concepts may not be copied, distributed, modified, or used for commercial purposes without explicit permission.
