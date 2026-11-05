// Relecture hors ligne avec le véritable gestionnaire Satellite, sans accès réseau ni radio.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
using byte = uint8_t;
using std::isnan;
struct RadioSchedule {};
class String : public std::string {
public:
    using std::string::string;
    String(const std::string& s): std::string(s) {}
    String(unsigned n): std::string(std::to_string(n)) {}
    String(int n): std::string(std::to_string(n)) {}
};
struct Preferences {};
struct MqttTopic {String full;};
struct MqttEntity {String value; MqttTopic stateTopic;};
struct MqttManager {
    unsigned publications = 0;
    void publishState(MqttEntity& entity, const String& value) {
        entity.value = value;
        ++publications;
    }
};
struct Config {};
constexpr int ID_CHAUDIERE = 0x80;
constexpr int RADIOLIB_ERR_NONE = 0;
constexpr int RADIOLIB_SX126X_MAX_PACKET_LENGTH = 255;
void delay(unsigned) {}
uint32_t millis() { return 100; }
void info(const char*, ...) {}
void error(const char*, ...) {}
void warning(const String&) {}
struct fword {
    byte b[2];
    uint16_t toUInt16() const { return (uint16_t(b[0]) << 8) | b[1]; }
};
struct temperature16 : fword {float toFloat() const { return toUInt16() / 10.0f; }};
struct Date {Date(const byte*) {}};
struct Chaudiere {
    struct ETAT_CHAUDIERE {};
    void setEtatChaudiere(byte) {}
    ETAT_CHAUDIERE getEtatChaudiere() {return {};}
};
#include "wire-types.inc"
struct FrisquetDevice {
    FrisquetRadio& link;
    byte id, association = 0, message = 0;
    FrisquetDevice(FrisquetRadio& radio, Config&, MqttManager&, byte id): link(radio), id(id) {}
    FrisquetRadio& radio() {return link;}
    byte getId() {return id;}
    byte getIdMessage() {return message;}
    byte getIdAssociation() {return association;}
    void setIdMessage(byte n) {message=n;}
    void setIdAssociation(byte n) {association=n;}
    void incrementIdMessage(byte n) {message+=n;}
    void setDate(Date) {}
};
#include "classes.inc"

void Zone::saveConfig() {}
void Zone::publishMqtt() {publierMode();}
uint8_t Zone::getNumeroZone() {return _idZone == 8 ? 1 : _idZone == 9 ? 2 : 3;}
uint8_t Zone::getIdZone() {return _idZone;}
bool Zone::programmeDisponible() const {return true;}
void Zone::setTemperatureAmbiante(float value) {_temperatureAmbiante=value;}
void Zone::setTemperatureConsigne(float value) {_temperatureConsigne=value;}
float Zone::getTemperatureConsigne() {return _temperatureConsigne;}
void Satellite::saveConfig() {}
void Satellite::publishMqtt() {}
bool Satellite::envoyerConsigne() {assert(false && "la relecture ne doit jamais envoyer de commande radio"); return false;}

#include "methods.inc"
#include "fixtures.inc"

std::vector<byte> unhex(const char* text) {
    std::vector<byte> bytes;
    for (size_t i=0; text[i]; i+=2) {
        unsigned n=0;
        assert(sscanf(text+i,"%2x", &n)==1);
        bytes.push_back(static_cast<byte>(n));
    }
    return bytes;
}

int main() {
    for (const auto& fixture : fixtures) {
        for (const auto source : {Zone::CONNECT, Zone::SATELLITE_PHYSIQUE}) {
            MqttManager mqtt; Config config; FrisquetRadio radio; Chaudiere boiler;
            Zone zone(8, mqtt); zone.setSource(source); zone._mqttPret=true;
            // Représenter le mode demandé enregistré ; le mode observé est initialement inconnu.
            zone.setMode(Zone::AUTO, true);
            Satellite satellite(radio, config, mqtt, zone, boiler);
            auto request=unhex(fixture.request);
            radio.reply=unhex(fixture.reply);
            assert(request.size()==23 && radio.reply.size()==49);
            assert(satellite.onReceive(request.data(), request.size()));
            printf("brut=0x%02X source=%s enregistré=%u observé=%u mode=%s régime=%s boost=%s\n",
                fixture.raw, source==Zone::CONNECT ? "Connect" : "physique", zone._mode,
                zone._modeObserve, zone._mqttEntities.preset.value.c_str(),
                zone._mqttEntities.regime.value.c_str(), zone._mqttEntities.boost.value.c_str());
            if (BASELINE && source==Zone::CONNECT) {
                assert(zone._modeObserve==Zone::INCONNU);
                assert(zone._mqttEntities.preset.value=="None");
            }
            if (BASELINE && fixture.raw==0x45) {
                // L'échec du décodage laisse en place l'ancienne demande Auto.
                assert(zone._mode==Zone::AUTO);
                if (source==Zone::SATELLITE_PHYSIQUE) assert(zone._modeObserve==Zone::AUTO);
            }
            if (!BASELINE) {
                const auto mode=(fixture.raw==0x25 || fixture.raw==0x20) ? Zone::AUTO : Zone::CONFORT;
                const char* preset=fixture.raw==0x45 ? "Boost" : mode==Zone::AUTO ? "Auto" : "Confort";
                assert(zone._modeObserve==mode);
                assert(zone._mqttEntities.preset.value==preset);
                assert(zone._mqttEntities.regime.value==(fixture.raw==0x20 ? "Réduit" : "Confort"));
                assert(zone._mqttEntities.boost.value==(fixture.raw==0x45 ? "ON" : "OFF"));
            }
        }
    }
    if (!BASELINE) {
        // Les modifications suivantes simulent des cas limites ; ce ne sont pas des captures.
        for (unsigned raw : {0x21,0x24,0x22,0x23,0x26,0x27,0x00,0x04,0x10,0x41,0x08,0x40,0x60,0x80,0xff}) {
            for (bool pending : {false,true}) {
                for (unsigned zoneNumber=1;zoneNumber<=3;++zoneNumber) {
                    MqttManager mqtt; Config config; FrisquetRadio radio; Chaudiere boiler;
                    const byte id=zoneNumber+7;
                    Zone zone(id,mqtt); zone.setSource(Zone::CONNECT); zone._mqttPret=true;
                    zone.setMode(Zone::CONFORT); zone.setModeOptions(0x41);
                    zone._lastChange=pending ? 2 : 0; zone._lastEnvoi=0;
                    Satellite satellite(radio,config,mqtt,zone,boiler);
                    auto request=unhex(fixtures[0].request); request[1]=id;
                    radio.reply=unhex(fixtures[0].reply); radio.reply[0]=id;
                    // Faire différer la réponse de la requête pour vérifier la source utilisée.
                    radio.reply[24+(zoneNumber-1)*10]=raw;
                    assert(satellite.onReceive(request.data(),request.size()));
                    const bool invalid=raw==0x08 || raw==0x40 || raw==0x60 || raw==0x80 || raw==0xff;
                    if (invalid) assert(zone._modeObserve==Zone::INCONNU);
                    else {
                        const auto expected=raw==0x10 ? Zone::HORS_GEL : (raw&0x22) ? Zone::AUTO : (raw&1) ? Zone::CONFORT : Zone::REDUIT;
                        assert(zone._modeObserve==expected);
                        if (raw&2) assert(zone._mqttEntities.preset.value==((raw&1) ? "Dérogation Confort" : "Dérogation Réduit"));
                        assert(bool(zone._optionsObservees&0x40)==bool(raw&0x40));
                    }
                    if (pending || invalid) {
                        assert(zone._mode==Zone::CONFORT && zone._modeOptions==0x41);
                    }
                }
            }
        }
        // Toute réponse chaudière tronquée est rejetée avant confirmation du mode.
        for (size_t size=0;size<49;++size) {
            MqttManager mqtt; Config config; FrisquetRadio radio; Chaudiere boiler;
            Zone zone(8,mqtt); zone.setSource(Zone::CONNECT); zone._mqttPret=true;
            Satellite satellite(radio,config,mqtt,zone,boiler);
            auto request=unhex(fixtures[0].request);
            radio.reply=unhex(fixtures[0].reply);radio.reply.resize(size);
            assert(!satellite.onReceive(request.data(),request.size()));
            assert(zone._modeObserve==Zone::INCONNU && mqtt.publications==0);
        }
    }
    puts(BASELINE ? "Défaut de la révision d'origine reproduit." : "Correctif validé : modes capturés et cas simulés de commandes en attente, sélection de zone, modes invalides et réponses tronquées.");
}
