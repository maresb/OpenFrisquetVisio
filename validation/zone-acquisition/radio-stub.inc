std::vector<byte> reply;
int receiveExpected(byte sender, byte destination, byte association, byte message,
                    byte reception, byte type, byte* output, size_t& length,
                    byte, bool) {
    if (reply.size()<sizeof(RadioTrameHeader)) return -1;
    // Reprendre les critères du transport actuel, qui ne vérifie pas l'association.
    (void)association;
    const auto* h=reinterpret_cast<const RadioTrameHeader*>(reply.data());
    if (h->idExpediteur!=sender || h->idDestinataire!=destination ||
        h->idMessage!=message ||
        h->idReception!=reception || h->type!=type) return -1;
    length=reply.size();
    memcpy(output, reply.data(), length);
    return RADIOLIB_ERR_NONE;
}
