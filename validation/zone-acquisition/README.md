# Récupération du mode des zones à partir des échanges radio ordinaires

Documents de référence pour la [PR #42](https://github.com/FreedomNX/OpenFrisquetVisio/pull/42).
Investigation et essai sur appareil réalisés le 5 novembre 2026, à partir de
la révision amont `9214063`.

Les [illustrations avant/après](../zone-state-recovery.md) présentent le
comportement visible dans Home Assistant.

## Constat

L'échange ordinaire entre le satellite physique et la chaudière contient déjà
l'information manquante. Le satellite écrit son octet de mode et d'options en
A02F ; la réponse de la chaudière contient un octet de même format pour chaque
zone. `Satellite::onReceive()` attend déjà cette réponse et la décode, mais
ignore ses champs de mode.

Le code ne confirme le mode du satellite que lorsque la source de la zone est
`SATELLITE_PHYSIQUE`. L'activation de Connect change cette source en
`CONNECT`. Le mode observé reste alors inconnu jusqu'à un échange distinct de
configuration des zones, en A154. Les lectures de Connect au démarrage et les
lectures périodiques ne récupèrent pas cette configuration.

Les anciens commentaires des champs inutilisés étaient trompeurs : il s'agit
d'indicateurs au format A02F (`0x25` signifie Auto/Confort), et non de
l'énumération A154 (`0x05` signifie Auto). Copier directement cet octet dans
`Zone::MODE_ZONE` serait incorrect.

## Éléments publics existants

- La [PR #30, fusionnée](https://github.com/FreedomNX/OpenFrisquetVisio/pull/30),
  documente des changements imposés depuis l'application Connect et le satellite
  physique. Elle étaye le décodage existant des modes Auto, permanents et des
  dérogations, notamment `0x22` pour une dérogation Réduit. Le mainteneur signale
  de possibles différences avec les anciens satellites ; cette investigation
  ne permet pas de les exclure.
- Le [ticket #7](https://github.com/FreedomNX/OpenFrisquetVisio/issues/7) fournit
  des paquets bruts accompagnés des actions effectuées dans Connect. Ses
  153 paquets donnent 15 paires requête A02F/réponse chaudière. L'octet de mode
  concorde dans chaque paire : `0x25` (2), `0x20` (2), `0x05` (2),
  `0x45` (7), `0x01` (2). Cela couvre Auto/Confort, Auto/Réduit, Confort
  permanent, ainsi que l'activation et la désactivation de Boost. Cette capture
  ne contient pas d'essai de dérogation temporaire ni de mode Hors Gel.

Les horodatages publiés ne servent pas à déduire une latence radio. Les
en-têtes concordants et le contenu des paquets établissent les échanges et
leurs valeurs. Cinq paires représentatives de cette capture publique sont
conservées dans `public-mode-pairs.json`.

## Capture avant correction

La capture utilisait le micrologiciel `26.11.04.2341`, avec les correctifs OTA
et portail déjà installés. Pendant plus de deux heures avant l'essai, le
journal de diagnostic montrait le traitement du satellite physique à dix
minutes d'intervalle.

Le mode développeur a été activé pour une capture passive de quinze minutes,
puis rétabli à sa valeur initiale, ce qui a provoqué deux redémarrages du pont.
Seul ce réglage a été modifié. Aucune commande de chauffage, requête radio
expérimentale, mise à jour du micrologiciel ou modification d'état Home Assistant
n'a été envoyée. Les échanges ordinaires du micrologiciel ont continué.

Après le premier redémarrage, une requête complète de 23 octets et sa réponse
chaudière de 49 octets contenaient toutes deux le mode `0x25`. Elles étaient
présentes à 187 secondes de fonctionnement ; le relevé précédent datait de
157 secondes. Une lecture de Home Assistant après cet échange renvoyait encore
`unknown` pour le thermostat, le mode actif et le régime actif. L'information
valide avait donc été reçue en environ trois minutes, sans être publiée.

Une seconde paire complète avec le même mode est arrivée dix minutes plus
tard ; les trois états étaient toujours `unknown`. La capture s'est terminée
à 900 secondes. Le mode développeur a été remis à `false` ; après le second
redémarrage, MQTT s'est reconnecté et le portail répondait.

Les journaux radio bruts et les horodatages Home Assistant précis restent
conservés localement. Seules les observations agrégées sont publiées ici.

## Correctif

Pour une zone Connect associée à un satellite physique observé, le pont décode
l'octet de la zone dans la réponse chaudière déjà reçue par
`Satellite::onReceive()`, puis publie cette observation. Pour une zone pilotée
uniquement par le satellite physique, la requête du satellite reste la source.
Le fonctionnement virtuel et les émissions radio sont inchangés.

Le correctif traite aussi deux cas nécessaires :

- L'ancien décodeur rejette `0x45`, identifié comme Boost dans la capture
  publique. Le correctif accepte Boost en Confort permanent et conserve cet
  indicateur. Les combinaisons Boost/Auto/dérogation non établies sont rejetées.
  `0x41` suit la règle existante selon laquelle le bit `0x04` ne change pas
  le mode ; ce cas est testé par simulation, pas présenté comme une capture.
- `setModeFromRaw()` ne signalait pas auparavant le succès du décodage.
  Un échec pouvait donc confirmer une ancienne valeur demandée. La méthode
  renvoie désormais un résultat, et la confirmation n'est publiée qu'en cas
  de succès. Une commande Connect en attente reste préservée pendant la
  publication du mode observé.

## Essai sur appareil

La révision d'intégration `d2ae84b` associe le correctif aux changements OTA et
portail déjà installés (`764ad12`). Le micrologiciel `26.11.05.1851` a été
téléversé avec succès par ArduinoOTA authentifié.

Après le redémarrage, les trois entités Home Assistant étaient initialement
`unknown`. Le premier échange ordinaire du satellite physique a rétabli :

| Entité | Après l'échange |
| --- | --- |
| Thermostat Z1 | `auto` |
| Mode actif Z1 | `Auto` |
| Régime actif Z1 | `Confort` |

Le rétablissement s'est produit entre les relevés à 428 et 458 secondes de
fonctionnement, soit environ 7 min 30 après le démarrage. Le journal ordinaire
a enregistré un échange du satellite et aucun échange de configuration A154.
Cela valide la récupération par la réponse ordinaire. Les trois valeurs sont
restées stables pendant la minute suivante. MQTT est resté connecté, le portail
répondait et de nouvelles requêtes mDNS ont confirmé la découverte du service
OTA et du nom d'hôte `.local`.

Aucune commande de chauffage ni requête radio expérimentale n'a été envoyée.
Le mode développeur est resté désactivé. Le délai mesuré dépend du moment de la
prochaine émission du satellite et de sa bonne réception ; il ne constitue ni
un délai fixe au démarrage, ni une borne supérieure. Les relevés bruts de
l'essai sont conservés localement.

## Autres vérifications et limites

`replay.py` compile les véritables méthodes de `Satellite::onReceive()`, du
décodeur, des modes de zone et de publication MQTT avec des substituts isolés
pour le matériel et MQTT. Le programme de test C++ se trouve dans
`replay.cpp` ; Python extrait le code source et lance le compilateur.
Le substitut radio fournit une réponse choisie ; il ne reproduit ni les
temporisations radio, ni les pertes de paquets, ni le pilote matériel.

```sh
python3 validation/zone-acquisition/replay.py validation/zone-acquisition/public-mode-pairs.json --baseline
python3 validation/zone-acquisition/replay.py validation/zone-acquisition/public-mode-pairs.json
python3 tests/test_satellite_mode.py
python3 tests/test_zone_modes.py
```

- Avec la révision d'origine, la relecture reproduit l'état Connect inconnu
  pour les cinq valeurs capturées, ainsi que le rejet de Boost et la
  confirmation erronée de l'ancien mode en fonctionnement physique.
- Avec le correctif, elle publie les valeurs attendues pour le mode, le régime
  et Boost. Les cas simulés couvrent les commandes en attente, les dérogations,
  Hors Gel, les octets de mode invalides, les trois zones et toutes les
  longueurs de réponse tronquée.
- La relecture de l'échange Auto/Confort capturé localement réussit également.
- Les tests natifs ont été exécutés avec GCC 15.2.0, AddressSanitizer et
  UndefinedBehaviorSanitizer (`ASAN_OPTIONS=detect_leaks=0` dans le bac à sable).
  Les tests existants du satellite et des zones réussissent. La compilation
  Heltec V3 réussit avec Arduino-ESP32 3.3.12 : 170 084 octets de RAM statique,
  1 724 323 octets de mémoire flash.

L'essai sur chaudière couvre la zone 1 en Auto/Confort ; les autres états
capturés proviennent de l'installation publique indépendante. La sélection
des zones 2 et 3 et les commandes en attente sont testées par simulation.
La compatibilité avec les anciens satellites reste à vérifier. Une zone sans
échange de satellite physique ne peut pas récupérer son mode par cette voie.
Une perte de réception peut retarder le rétablissement : dix minutes est une
cadence observée, pas une borne.

Les autres modes, les dérogations et Boost n'ont pas été essayés sur la
chaudière locale : ces essais demanderaient des changements de chauffage
coordonnés.
