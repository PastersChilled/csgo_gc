#include "stdafx.h"
#include "gc_client.h"
#include "graffiti.h"
#include "keyvalue.h"

ClientGC::ClientGC(uint64_t steamId)
    : m_steamId{ steamId }
    , m_inventory{ steamId }
{
    Graffiti::Initialize();
    StartThread();
    Platform::Print("ClientGC spawned for user %llu\n", steamId);
}

ClientGC::~ClientGC()
{
    m_searching = false;
    if (m_matchmakingThread.joinable())
        m_matchmakingThread.join();
    StopThread();
    Platform::Print("ClientGC destroyed\n");
}

void ClientGC::HandleEvent(GCEvent type, uint64_t id, const std::vector<uint8_t> &buffer)
{
    switch (type)
    {
    case GCEvent::Message:
        HandleMessage(static_cast<uint32_t>(id), buffer.data(), static_cast<uint32_t>(buffer.size()));
        break;
    case GCEvent::NetMessage:
        HandleNetMessage(buffer.data(), static_cast<uint32_t>(buffer.size()));
        break;
    case GCEvent::SOCacheRequest:
        HandleSOCacheRequest();
        break;
    default:
        assert(false);
        break;
    }
}

void ClientGC::HandleMessage(uint32_t type, const void *data, uint32_t size)
{
    GCMessageRead messageRead{ type, data, size };
    if (!messageRead.IsValid())
    {
        assert(false);
        return;
    }

    if (messageRead.IsProtobuf())
    {
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGCClientHello:
            OnClientHello(messageRead);
            break;
        case k_EMsgGCAdjustItemEquippedState:
            AdjustItemEquippedState(messageRead);
            break;
        case k_EMsgGCCStrike15_v2_ClientPlayerDecalSign:
            ClientPlayerDecalSign(messageRead);
            break;
        case k_EMsgGCUseItemRequest:
            UseItemRequest(messageRead);
            break;
        case k_EMsgGCCStrike15_v2_ClientRequestJoinServerData:
            ClientRequestJoinServerData(messageRead);
            break;
        case k_EMsgGCSetItemPositions:
            SetItemPositions(messageRead);
            break;
        case k_EMsgGCApplySticker:
            ApplySticker(messageRead);
            break;
        case k_EMsgGCCStrike15_v2_MatchmakingStart:
            OnMatchmakingStart(messageRead);
            break;
        case k_EMsgGCCStrike15_v2_MatchmakingStop:
            OnMatchmakingStop(messageRead);
            break;
        case k_EMsgGCStoreGetUserData:
            StoreGetUserData(messageRead);
            break;
        case k_EMsgGCStorePurchaseInit:
            StorePurchaseInit(messageRead);
            break;
        case k_EMsgGCStorePurchaseFinalize:
            StorePurchaseFinalize(messageRead);
            break;
        default:
            Platform::Print("ClientGC::HandleMessage: unhandled protobuf message %s\n",
                MessageName(messageRead.TypeUnmasked()));
            break;
        }
    }
    else
    {
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGCDelete:
            DeleteItem(messageRead);
            break;
        case k_EMsgGCUnlockCrate:
            UnlockCrate(messageRead);
            break;
        case k_EMsgGCNameItem:
            NameItem(messageRead);
            break;
        case k_EMsgGCNameBaseItem:
            NameBaseItem(messageRead);
            break;
        case k_EMsgGCRemoveItemName:
            RemoveItemName(messageRead);
            break;
        default:
            Platform::Print("ClientGC::HandleMessage: unhandled struct message %s\n",
                MessageName(messageRead.TypeUnmasked()));
            break;
        }
    }
}

void ClientGC::HandleNetMessage(const void *data, uint32_t size)
{
    GCMessageRead messageRead{ 0, data, size };
    if (!messageRead.IsValid())
    {
        assert(false);
        return;
    }

    if (messageRead.IsProtobuf())
    {
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGC_IncrementKillCountAttribute:
            IncrementKillCountAttribute(messageRead);
            return;
        }
    }

    Platform::Print("ClientGC::HandleNetMessage: unhandled protobuf message %s\n",
        MessageName(messageRead.TypeUnmasked()));
}

void ClientGC::HandleSOCacheRequest()
{
    CMsgSOCacheSubscribed message;
    m_inventory.BuildCacheSubscription(message, GetConfig().Level(), true);

    GCMessageWrite messageWrite{ k_ESOMsg_CacheSubscribed, message };
    PostToHost(HostEvent::NetMessage, 0, messageWrite.Data(), messageWrite.Size());
}

void ClientGC::SendMessageToGame(bool sendToGameServer, uint32_t type,
    const google::protobuf::MessageLite &message, uint64_t jobId)
{
    GCMessageWrite messageWrite{ type, message, jobId };

    if (sendToGameServer)
        PostToHost(HostEvent::NetMessage, 0, messageWrite.Data(), messageWrite.Size());

    PostToHost(HostEvent::Message, messageWrite.TypeMasked(), messageWrite.Data(), messageWrite.Size());
}

constexpr uint32_t MakeAddress(uint32_t v1, uint32_t v2, uint32_t v3, uint32_t v4)
{
    return v4 | (v3 << 8) | (v2 << 16) | (v1 << 24);
}

static void BuildCSWelcome(CMsgCStrike15Welcome &message)
{
    message.set_store_item_hash(136617352);
    message.set_timeplayedconsecutively(0);
    message.set_time_first_played(1329845773);
    message.set_last_time_played(1680260376);
    message.set_last_ip_address(MakeAddress(127, 0, 0, 1));
}

void ClientGC::BuildMatchmakingHello(CMsgGCCStrike15_v2_MatchmakingGC2ClientHello &message)
{
    message.set_account_id(AccountId());

    message.mutable_global_stats()->set_players_online(12345);
    message.mutable_global_stats()->set_servers_online(500);
    message.mutable_global_stats()->set_players_searching(1337);
    message.mutable_global_stats()->set_servers_available(250);
    message.mutable_global_stats()->set_ongoing_matches(300);
    message.mutable_global_stats()->set_search_time_avg(45);
    message.mutable_global_stats()->set_main_post_url("");
    message.mutable_global_stats()->set_required_appid_version(13857);
    message.mutable_global_stats()->set_pricesheet_version(1680057676);
    message.mutable_global_stats()->set_twitch_streams_version(2);
    message.mutable_global_stats()->set_active_tournament_eventid(20);
    message.mutable_global_stats()->set_active_survey_id(0);
    message.mutable_global_stats()->set_required_appid_version2(13862);

    message.set_vac_banned(GetConfig().VacBanned());
    message.set_penalty_seconds(0);
    message.set_penalty_reason(0);
    message.mutable_commendation()->set_cmd_friendly(GetConfig().CommendedFriendly());
    message.mutable_commendation()->set_cmd_teaching(GetConfig().CommendedTeaching());
    message.mutable_commendation()->set_cmd_leader(GetConfig().CommendedLeader());
    message.set_player_level(GetConfig().Level());
    message.set_player_cur_xp(GetConfig().Xp());

    PlayerRankingInfo *rank = message.mutable_ranking();
    rank->set_account_id(AccountId());
    rank->set_rank_id(GetConfig().CompetitiveRank());
    rank->set_wins(GetConfig().CompetitiveWins());
    rank->set_rank_type_id(RankTypeCompetitive);
}

void ClientGC::BuildClientWelcome(CMsgClientWelcome &message, const CMsgCStrike15Welcome &csWelcome,
    const CMsgGCCStrike15_v2_MatchmakingGC2ClientHello &matchmakingHello)
{
    message.set_version(0);
    message.set_game_data(csWelcome.SerializeAsString());
    m_inventory.BuildCacheSubscription(*message.add_outofdate_subscribed_caches(), GetConfig().Level(), false);
    message.mutable_location()->set_latitude(65.0133006f);
    message.mutable_location()->set_longitude(25.4646212f);
    message.mutable_location()->set_country("FI");
    message.set_game_data2(matchmakingHello.SerializeAsString());
    message.set_rtime32_gc_welcome_timestamp(static_cast<uint32_t>(time(nullptr)));
    message.set_currency(2);
    message.set_txn_country_code("FI");
}

void ClientGC::SendRankUpdate()
{
    CMsgGCCStrike15_v2_ClientGCRankUpdate message;

    PlayerRankingInfo *rank = message.add_rankings();
    rank->set_account_id(AccountId());
    rank->set_rank_id(GetConfig().CompetitiveRank());
    rank->set_wins(GetConfig().CompetitiveWins());
    rank->set_rank_type_id(RankTypeCompetitive);

    rank = message.add_rankings();
    rank->set_account_id(AccountId());
    rank->set_rank_id(GetConfig().WingmanRank());
    rank->set_wins(GetConfig().WingmanWins());
    rank->set_rank_type_id(RankTypeWingman);

    rank = message.add_rankings();
    rank->set_account_id(AccountId());
    rank->set_rank_id(GetConfig().DangerZoneRank());
    rank->set_wins(GetConfig().DangerZoneWins());
    rank->set_rank_type_id(RankTypeDangerZone);

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientGCRankUpdate, message);
}

void ClientGC::OnClientHello(GCMessageRead &messageRead)
{
    CMsgClientHello hello;
    if (!messageRead.ReadProtobuf(hello))
    {
        Platform::Print("Parsing CMsgClientHello failed, ignoring\n");
        return;
    }

    CMsgCStrike15Welcome csWelcome;
    BuildCSWelcome(csWelcome);

    CMsgGCCStrike15_v2_MatchmakingGC2ClientHello mmHello;
    BuildMatchmakingHello(mmHello);

    CMsgClientWelcome clientWelcome;
    BuildClientWelcome(clientWelcome, csWelcome, mmHello);

    SendMessageToGame(false, k_EMsgGCClientWelcome, clientWelcome);
    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientHello, mmHello);
    SendRankUpdate();
}

void ClientGC::AdjustItemEquippedState(GCMessageRead &messageRead)
{
    CMsgAdjustItemEquippedState message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgAdjustItemEquippedState failed, ignoring\n");
        return;
    }

    CMsgSOMultipleObjects update;
    if (!m_inventory.EquipItem(message.item_id(), message.new_class(), message.new_slot(), update))
    {
        assert(false);
        return;
    }

    SendMessageToGame(true, k_ESOMsg_UpdateMultiple, update);
}

void ClientGC::ClientPlayerDecalSign(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_ClientPlayerDecalSign message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_ClientPlayerDecalSign failed, ignoring\n");
        return;
    }

    if (!Graffiti::SignMessage(*message.mutable_data()))
    {
        Platform::Print("Could not sign graffiti! it won't appear\n");
        return;
    }

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientPlayerDecalSign, message);
}

void ClientGC::UseItemRequest(GCMessageRead &messageRead)
{
    CMsgUseItem message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgUseItem failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject destroy;
    CMsgSOMultipleObjects updateMultiple;
    CMsgGCItemCustomizationNotification notification;

    if (m_inventory.UseItem(message.item_id(), destroy, updateMultiple, notification))
    {
        SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
        SendMessageToGame(true, k_ESOMsg_UpdateMultiple, updateMultiple);
        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
    }
}

static void AddressString(uint32_t ip, uint32_t port, char *buffer, size_t bufferSize)
{
    snprintf(buffer, bufferSize,
        "%u.%u.%u.%u:%u\n",
        (ip >> 24) & 0xff,
        (ip >> 16) & 0xff,
        (ip >> 8) & 0xff,
        ip & 0xff,
        port);
}

void ClientGC::ClientRequestJoinServerData(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_ClientRequestJoinServerData request;
    if (!messageRead.ReadProtobuf(request))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_ClientRequestJoinServerData failed, ignoring\n");
        return;
    }

    CMsgGCCStrike15_v2_ClientRequestJoinServerData response = request;
    response.mutable_res()->set_serverid(request.version());
    response.mutable_res()->set_direct_udp_ip(request.server_ip());
    response.mutable_res()->set_direct_udp_port(request.server_port());
    response.mutable_res()->set_reservationid(GameServerCookieId);

    char addressString[32];
    AddressString(request.server_ip(), request.server_port(), addressString, sizeof(addressString));
    response.mutable_res()->set_server_address(addressString);

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientRequestJoinServerData, response);
}

void ClientGC::SetItemPositions(GCMessageRead &messageRead)
{
    CMsgSetItemPositions message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgSetItemPositions failed, ignoring\n");
        return;
    }

    std::vector<CMsgItemAcknowledged> acknowledgements;
    acknowledgements.reserve(message.item_positions_size());

    CMsgSOMultipleObjects update;
    if (m_inventory.SetItemPositions(message, acknowledgements, update))
    {
        for (const CMsgItemAcknowledged &acknowledgement : acknowledgements)
        {
            GCMessageWrite messageWrite{ k_EMsgGCItemAcknowledged, acknowledgement };
            PostToHost(HostEvent::NetMessage, 0, messageWrite.Data(), messageWrite.Size());
        }
        SendMessageToGame(true, k_ESOMsg_UpdateMultiple, update);
    }
    else
    {
        assert(false);
    }
}

void ClientGC::IncrementKillCountAttribute(GCMessageRead &messageRead)
{
    CMsgIncrementKillCountAttribute message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgIncrementKillCountAttribute failed, ignoring\n");
        return;
    }

    assert(message.event_type() == 0);

    CMsgSOSingleObject update;
    if (m_inventory.IncrementKillCountAttribute(message.item_id(), message.amount(), update))
        SendMessageToGame(true, k_ESOMsg_Update, update);
    else
        assert(false);
}

void ClientGC::ApplySticker(GCMessageRead &messageRead)
{
    CMsgApplySticker message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgApplySticker failed, ignoring\n");
        return;
    }

    assert(!message.item_item_id() != !message.baseitem_defidx());

    CMsgSOSingleObject update, destroy;
    CMsgGCItemCustomizationNotification notification;

    if (!message.sticker_item_id())
    {
        if (m_inventory.ScrapeSticker(message, update, destroy, notification))
        {
            if (destroy.has_type_id())
                SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
            if (update.has_type_id())
                SendMessageToGame(true, k_ESOMsg_Update, update);
            if (notification.has_request())
                SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
        }
        else
        {
            assert(false);
        }
    }
    else if (m_inventory.ApplySticker(message, update, destroy, notification))
    {
        SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
        SendMessageToGame(true, k_ESOMsg_Update, update);
        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
    }
    else
    {
        assert(false);
    }
}

void ClientGC::StoreGetUserData(GCMessageRead &messageRead)
{
    CMsgStoreGetUserData message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgStoreGetUserData failed, ignoring\n");
        return;
    }

    KeyValue priceSheet{ "price_sheet" };
    if (!priceSheet.ParseFromFile("csgo_gc/price_sheet.txt"))
        return;

    std::string binaryString;
    binaryString.reserve(1 << 17);
    priceSheet.BinaryWriteToString(binaryString);

    CMsgStoreGetUserDataResponse response;
    response.set_result(1);
    response.set_price_sheet_version(1729);
    *response.mutable_price_sheet() = std::move(binaryString);

    SendMessageToGame(false, k_EMsgGCStoreGetUserDataResponse, response);
}

void ClientGC::StorePurchaseInit(GCMessageRead &messageRead)
{
    CMsgGCStorePurchaseInit message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgGCStorePurchaseInit failed, ignoring\n");
        return;
    }

    uint64_t transactionId = Random{}.Integer<uint64_t>();
    assert(!m_transactionId);
    m_transactionId = transactionId;
    m_transactionItemIds.reserve(message.line_items_size());

    std::vector<CMsgSOSingleObject> inventoryUpdate;

    for (const auto &item : message.line_items())
    {
        for (uint32_t i = 0; i < item.quantity(); i++)
        {
            uint64_t itemId = m_inventory.PurchaseItem(item.item_def_id(), inventoryUpdate);
            if (!itemId)
                assert(false);
            else
                m_transactionItemIds.push_back(itemId);
        }
    }

    char url[128];
    snprintf(url, sizeof(url), "https://checkout.steampowered.com/checkout/approvetxn/%llu/?returnurl=steam", transactionId);

    CMsgGCStorePurchaseInitResponse response;
    response.set_result(1);
    response.set_txn_id(transactionId);
    response.set_url(url);
    response.mutable_item_ids()->Assign(m_transactionItemIds.begin(), m_transactionItemIds.end());

    SendMessageToGame(false, k_EMsgGCStorePurchaseInitResponse, response, messageRead.JobId());

    for (auto &newItem : inventoryUpdate)
        SendMessageToGame(true, k_ESOMsg_Create, newItem);

    PostToHost(HostEvent::MicroTransactionResponse, 0, nullptr, 0);
}

void ClientGC::StorePurchaseFinalize(GCMessageRead &messageRead)
{
    CMsgGCStorePurchaseFinalize message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgGCStorePurchaseFinalize failed, ignoring\n");
        return;
    }

    assert(m_transactionId);

    CMsgGCStorePurchaseFinalizeResponse response;
    response.set_result(1);
    response.mutable_item_ids()->Assign(m_transactionItemIds.begin(), m_transactionItemIds.end());
    SendMessageToGame(false, k_EMsgGCStorePurchaseFinalizeResponse, response, messageRead.JobId());

    m_transactionId = 0;
}

void ClientGC::DeleteItem(GCMessageRead &messageRead)
{
    uint64_t itemId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCDelete failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject destroyed;
    if (m_inventory.RemoveItem(itemId, destroyed))
        SendMessageToGame(true, k_ESOMsg_Destroy, destroyed);
    else
        assert(false);
}

void ClientGC::UnlockCrate(GCMessageRead &messageRead)
{
    uint64_t keyId = messageRead.ReadUint64();
    uint64_t crateId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCUnlockCrate failed, ignoring\n");
        return;
    }

    Platform::Print("CASE OPENING %llu with %llu\n", crateId, keyId);

    CMsgSOSingleObject destroyCrate, destroyKey, newItem;
    CMsgGCItemCustomizationNotification notification;

    if (m_inventory.UnlockCrate(crateId, keyId, destroyCrate, destroyKey, newItem, notification))
    {
        SendMessageToGame(true, k_ESOMsg_Destroy, destroyCrate);
        SendMessageToGame(true, k_ESOMsg_Destroy, destroyKey);
        SendMessageToGame(true, k_ESOMsg_Create, newItem);
        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
    }
    else
    {
        assert(false);
    }
}

void ClientGC::NameItem(GCMessageRead &messageRead)
{
    uint64_t nameTagId = messageRead.ReadUint64();
    uint64_t itemId = messageRead.ReadUint64();
    messageRead.ReadData(1);
    std::string_view name = messageRead.ReadString();

    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCNameItem failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject update, destroy;
    CMsgGCItemCustomizationNotification notification;
    if (m_inventory.NameItem(nameTagId, itemId, name, update, destroy, notification))
    {
        SendMessageToGame(true, k_ESOMsg_Update, update);
        SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
    }
    else
    {
        assert(false);
    }
}

void ClientGC::NameBaseItem(GCMessageRead &messageRead)
{
    uint64_t nameTagId = messageRead.ReadUint64();
    uint32_t defIndex = messageRead.ReadUint32();
    messageRead.ReadData(1);
    std::string_view name = messageRead.ReadString();

    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCNameBaseItem failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject create, destroy;
    CMsgGCItemCustomizationNotification notification;
    if (m_inventory.NameBaseItem(nameTagId, defIndex, name, create, destroy, notification))
    {
        SendMessageToGame(true, k_ESOMsg_Create, create);
        SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
    }
    else
    {
        assert(false);
    }
}

void ClientGC::RemoveItemName(GCMessageRead &messageRead)
{
    uint64_t itemId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCRemoveItemName failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject update, destroy;
    CMsgGCItemCustomizationNotification notification;
    if (m_inventory.RemoveItemName(itemId, update, destroy, notification))
    {
        if (update.has_type_id())
            SendMessageToGame(true, k_ESOMsg_Update, update);
        if (destroy.has_type_id())
            SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
    }
    else
    {
        assert(false);
    }
}

// ============================================================
// BOT MATCHMAKING SIMULATION
// ============================================================

std::string ClientGC::PickMapForGameType(uint32_t gameType)
{
    static const char *compMaps[] = {
        "de_dust2", "de_mirage", "de_inferno", "de_nuke",
        "de_overpass", "de_vertigo", "de_ancient"
    };
    static const char *wingmanMaps[] = {
        "de_shortdust", "de_lake", "de_shortnuke", "de_train"
    };

    bool isWingman = (gameType & 1024) != 0;
    if (isWingman)
        return wingmanMaps[rand() % 4];
    return compMaps[rand() % 7];
}

void ClientGC::SendMatchmakingUpdate(int state)
{
    CMsgGCCStrike15_v2_MatchmakingGC2ClientUpdate update;
    update.set_matchmaking(state);

    auto *gs = update.mutable_global_stats();
    gs->set_players_online(12345);
    gs->set_servers_online(500);
    gs->set_players_searching(1337);
    gs->set_servers_available(250);
    gs->set_ongoing_matches(300);
    gs->set_search_time_avg(45);
    gs->set_required_appid_version(13857);
    gs->set_pricesheet_version(1680057676);

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientUpdate, update);
    Platform::Print("ClientGC: matchmaking state -> %d\n", state);
}

void ClientGC::LaunchBotServer(uint32_t gameType, const std::string &map)
{
    int rank = GetConfig().CompetitiveRank();
    int botDifficulty = 0;
    int botQuota = 9;

    if (rank >= 15)      botDifficulty = 3;
    else if (rank >= 10) botDifficulty = 2;
    else if (rank >= 5)  botDifficulty = 1;
    else                 botDifficulty = 0;

    if ((gameType & 1024) != 0) botQuota = 3;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "start /B srcds.exe -game csgo -console -usercon +game_type 0 +game_mode 1 "
        "+map %s +bot_quota %d +bot_difficulty %d "
        "+bot_join_after_player 0 +bot_chatter off "
        "+sv_lan 1 +sv_cheats 0 +exec gamemode_competitive "
        "-port 27015 -maxplayers_override 10",
        map.c_str(), botQuota, botDifficulty);

    Platform::Print("ClientGC: launching bot server: %s\n", cmd);
    system(cmd);
}

void ClientGC::SendMatchFound()
{
    std::string map = PickMapForGameType(m_searchGameType);
    LaunchBotServer(m_searchGameType, map);

    std::this_thread::sleep_for(std::chrono::seconds(3));

    uint32_t localIp = (127u << 24) | 1u;

    CMsgGCCStrike15_v2_MatchmakingGC2ClientReserve reserve;
    reserve.set_serverid(1337);
    reserve.set_direct_udp_ip(localIp);
    reserve.set_direct_udp_port(27015);
    reserve.set_reservationid(0xDEADBEEF);
    reserve.set_map(map);
    reserve.set_server_address("127.0.0.1:27015\n");

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientReserve, reserve);
    Platform::Print("ClientGC: match found! map=%s\n", map.c_str());
}

void ClientGC::OnMatchmakingStart(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_MatchmakingStart msg;
    if (!messageRead.ReadProtobuf(msg))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_MatchmakingStart failed\n");
        return;
    }

    if (m_searching)
    {
        Platform::Print("ClientGC: already searching, ignoring\n");
        return;
    }

    m_searching = true;
    m_searchGameType = msg.game_type();

    Platform::Print("ClientGC: matchmaking started, game_type=%u\n", m_searchGameType);
    SendMatchmakingUpdate(1);

    if (m_matchmakingThread.joinable())
        m_matchmakingThread.join();

    m_matchmakingThread = std::thread([this]()
    {
        int waitSeconds = 5 + (rand() % 15);
        Platform::Print("ClientGC: searching... ETA %d sec\n", waitSeconds);
        std::this_thread::sleep_for(std::chrono::seconds(waitSeconds));

        if (!m_searching) return;

        SendMatchFound();
        m_searching = false;
    });
}

void ClientGC::OnMatchmakingStop(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_MatchmakingStop msg;
    if (!messageRead.ReadProtobuf(msg))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_MatchmakingStop failed\n");
        return;
    }

    m_searching = false;
    SendMatchmakingUpdate(0);
    Platform::Print("ClientGC: matchmaking stopped\n");
}

void ClientGC::UpdateRankAfterMatch(bool won)
{
    RankId current = GetConfig().CompetitiveRank();
    int newRank = static_cast<int>(current);

    if (won && newRank < 18) newRank++;
    else if (!won && newRank > 1) newRank--;

    Platform::Print("ClientGC: rank update %d -> %d (win=%d)\n", current, newRank, (int)won);

    CMsgGCCStrike15_v2_ClientGCRankUpdate message;
    PlayerRankingInfo *rank = message.add_rankings();
    rank->set_account_id(AccountId());
    rank->set_rank_id(static_cast<RankId>(newRank));
    rank->set_wins(GetConfig().CompetitiveWins() + (won ? 1 : 0));
    rank->set_rank_type_id(RankTypeCompetitive);

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientGCRankUpdate, message);
}
