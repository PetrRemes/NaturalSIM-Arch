

# NaturalSIM — Architecture

> Procedurální simulace světa v Unreal Engine 5, zaměřená na emergentní chování přírody a civilizace.

NaturalSIM je experimentální simulace propojeného světa, ve kterém spolu vzájemně reagují přírodní, klimatické a lidské systémy.

Cílem není vytvořit svět složený z předem připravených scénářů, ale systém, ve kterém mohou dlouhodobé změny vznikat z interakce jednotlivých subsystémů.

Klima ovlivňuje prostředí, prostředí ovlivňuje flóru a faunu, dostupnost zdrojů ovlivňuje člověka a lidská činnost následně zpětně mění svět kolem něj.

---

## Základ simulace

<img width="1536" height="1024" alt="Schéma systémů" src="https://github.com/user-attachments/assets/3c486f82-57c5-486c-9ecc-1e712004f90c" />

Základem projektu je modulární architektura, ve které jednotlivé systémy fungují samostatně, ale zároveň si mezi sebou předávají data potřebná pro simulaci celého světa.

Centrální **World Manager** spravuje svět a koordinuje jednotlivé části simulace.

**Simulation Director** rozděluje výpočty v čase podle aktuální potřeby simulace, aby se náročné operace nepotkávaly ve stejném okamžiku a nezpůsobovaly zbytečné zámrzy.

Svět je rozdělen do **Chunk Systemu**, který umožňuje pracovat s jednotlivými oblastmi a řídit jejich aktivitu.

Jednotlivé systémy využívají společnou datovou vrstvu jednotlivých buněk světa. Díky tomu mohou číst a zapisovat informace o prostředí bez nutnosti vytvářet pevné vazby mezi každým systémem navzájem.

---

## Klíčové subsystémy

- **Climate & Hydro System:** Globální klima, atmosférické vlivy, srážky, povrchová a podzemní voda
- **Tectonic & World Generator:** Procedurální generování terénu, desková tektonika a geologické změny
- **Flora & Fauna System:** Ekosystémy, biomy, růst vegetace, pohyb a dynamika populací
- **Human System:** Populace, migrace, obživa, reakce na prostředí a vývoj znalostí
- **Settlement System:** Osady, budovy, infrastruktura, ekonomika, růst a expanze
- **Civilization System:** Národy, kultura, technologie, diplomacie a dlouhodobý vývoj civilizací
- **Transport System:** Cesty, lodě, obchodní trasy a propojení osídlení
- **Disaster System:** Přírodní katastrofy a jejich dopady na přírodní i lidské systémy
- **History System:** Záznam významných událostí, změn a dlouhodobého vývoje světa

---

## Datová vrstva světa

Jedním z hlavních principů architektury je oddělení jednotlivých systémů pomocí datové vrstvy světa.

Každá buňka obsahuje informace o svém aktuálním stavu a jednotlivé systémy z ní mohou číst data, která potřebují pro vlastní výpočty.

Například Flora System nemusí přímo komunikovat s Climate System.

Místo toho čte z příslušné buňky informace o teplotě, vlhkosti, vodě nebo dalších vlastnostech prostředí.

Stejný princip umožňuje jednotlivým subsystémům zůstat relativně nezávislými a zároveň vytvářet propojený svět.

---

## Simulace a vizualizace

Simulační logika je oddělena od vizualizační vrstvy.

**Simulace** pracuje především s daty a stavem světa.

**Vizualizace** převádí tato data do výsledné podoby terénu, vody, vegetace, fauny, lidí, osídlení a dalších prvků.

Díky tomuto oddělení nemusí být kompletní vizualizace celého světa neustále přepočítávána pouze proto, že se změnil stav simulace.

Svět tak může pokračovat ve výpočtech nezávisle na tom, co právě zobrazuje kamera.

---

## Výkon a optimalizace

Při vývoji jsem narazil na problém, kdy se více náročných výpočtů potkávalo ve stejném ticku a způsobovalo pravidelné zámrzy.

Řešením bylo rozdělit výpočty do jednotlivých časových částí a vytvořit vlastní systém jejich řízení.

### Simulation Director

Simulation Director sleduje aktuální potřeby simulace a rozděluje práci mezi jednotlivé tick slices.

Cílem je zabránit tomu, aby všechny systémy prováděly náročné operace najednou, a vytvořit plynulejší rozložení výpočtů v čase.

Projekt dále využívá principy jako:

- Asynchronous / background calculations
- Chunk-based simulation
- LOD
- HISM / Instancing
- Streaming
- Profiling & Telemetry

---

## Emergentní chování

NaturalSIM není založen pouze na předem připravených událostech.

Jednotlivé systémy mají vlastní pravidla a jejich vzájemná interakce může vytvářet dlouhodobé důsledky, které nemusí být konkrétně definované jako samostatný scénář.

Například změna prostředí může ovlivnit dostupnost potravy, ta může změnit pohyb populace, následně vznik nového osídlení a později další vývoj civilizace.

Smyslem je vytvořit svět, ve kterém nejsou jednotlivé systémy izolované, ale společně vytvářejí historii.

---

## Současný stav

NaturalSIM je stále aktivní prototyp.

Základní simulační jádro a řada jednotlivých systémů již fungují a jsou vzájemně propojené. Další části simulace a vizuální prezentace jsou stále ve vývoji.

Simulace byla testována také v výrazně zrychleném čase a dokázala běžet nepřetržitě přes **1000 simulovaných let bez pádu systému**.

---

## Moje role

Na projektu se soustředím především na:

- návrh simulační architektury
- návrh jednotlivých systémů a jejich vzájemných vazeb
- herní a simulační logiku
- datové struktury a tok informací
- návrh optimalizačních řešení
- prototypování a integraci
- testování a hledání problémů
- vizuální prezentaci simulace

Při implementaci intenzivně využívám **AI jako nástroj pro tvorbu a úpravu kódu**.

Nejsem profesionální C++ programátor. Moje hlavní role spočívá v návrhu logiky a architektury, rozhodování o tom, jak mají jednotlivé části systému spolupracovat, integraci výsledného kódu, testování a další iteraci projektu.

---

## Princip projektu

> **Žádný pevný scénář. Živý svět.**

NaturalSIM je experiment s otázkou, co se stane, když dostaneme jednotlivým systémům vlastní pravidla, data a vzájemné vazby a necháme je dlouhodobě působit jeden na druhý.

Místo předem napsaného příběhu vzniká prostor, ve kterém může příběh vzniknout z vývoje samotného světa.
