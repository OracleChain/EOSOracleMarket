#include "eosiolib/eosio.hpp"

#include "oraclemarket.hpp"

using namespace eosio;

//@abi action
void OracleMarket::mortgage(account_name from, account_name server, const asset &quantity){
    require_auth(from);

    transferfromact tf(from, currentAdmin, quantity);
    transferFromInline(tf);

    ContractInfo userScores(_self, server);
    auto serverIte = userScores.find(SCORES_INDEX);
    eosio_assert(serverIte != userScores.end(), CONTRACT_NOT_REGISTER_STILL);

    Mortgaged mt(_self, from);
    if(mt.find(from) == mt.end()){

        std::vector<mortgagepair> mortgegelist;
        mortgagepair mp(server, quantity, now());
        mortgegelist.push_back(mp);

        mortgaged m(from, mortgegelist);
        mt.emplace(from, [&]( auto& s ) {
            s.from = from;
            s.mortgegelist = mortgegelist;
        });
    }else{
        auto itefrom = mt.find(from);
        mortgagepair mp(server, quantity, now());
        mt.modify(itefrom, from, [&](auto &s){
            s.mortgegelist.push_back(mp);
        });
    }
    eosio::print("mortgage!");
}

//@abi action
void OracleMarket::unfrosse(account_name server, account_name from, const asset & quantity){
    require_auth(server);

    Mortgaged mt(_self, from);
    auto mortIte = mt.find(from);
    eosio_assert(mortIte!=mt.end(), USER_NOT_MORTGAGED_CANNOT_RELEASE);


    std::vector<mortgagepair> mortgegelist = mortIte->mortgegelist;
    //find first
    for(auto ite = mortgegelist.begin();ite != mortgegelist.end(); ite++){

          if(quantity == ite->quantity && ite->server == server){
              ite->status = STATUS_MORTGAGE_PAIR_CAN_FREEZE;
              break;
          }
    }

    mt.modify(*mortIte, server, [&](auto & s){
        s.mortgegelist = mortgegelist;
        s.from = from;
    });
}

#include "./eosiolib/crypto.h"
checksum256 cal_sha256(int64_t word)
{
    checksum256 cs = { 0 };
    char d[255] = { 0 };
    snprintf(d, sizeof(d) - 1, "%lld", word);
    sha256(d, strlen(d), &cs);

    return cs;
}

string cal_sha256_str(int64_t word)
{
    string h;
    checksum256 cs = cal_sha256(word);
    for (int i = 0; i < sizeof(cs.hash); ++i) {
        char hex[3] = { 0 };
        snprintf(hex, sizeof(hex), "%02x", static_cast<unsigned char>(cs.hash[i]));
        h += hex;
    }

    return h;
}

//@abi action
void OracleMarket::withdrawfro(account_name from){//withdrawfrozened
    require_auth(from);

    Mortgaged mt(_self, from);
    eosio_assert(mt.find(from)!=mt.end(), USER_NOT_MORTGAGED_CANNOT_RELEASE);

    auto mortIte = mt.get(from);

    uint32_t countEvilNotDealed = getEvilCountBySetStatus(from);
    int64_t amountPunishMent = evilBehaviorOCTPunishment*countEvilNotDealed;

    asset as;

    for(auto ite = mortIte.mortgegelist.begin();ite != mortIte.mortgegelist.end(); ite++){
          as = ite->quantity;

          ContractInfo conInfo(_self, ite->server);
          auto serverIte = conInfo.find(SCORES_INDEX);

          if(ite->createtime+serverIte->assfrosec<now() || ite->status == STATUS_MORTGAGE_PAIR_CAN_FREEZE){
               ite = mortIte.mortgegelist.erase(ite);
               amountPunishMent -= ite->quantity.amount;

               if(ite==mortIte.mortgegelist.end()){
                    break;
               }
          }
    }

    eosio_assert(amountPunishMent<0, AMOUNT_CAN_WITHDRAW_NOW_IS_ZERO);
    as.amount = (-amountPunishMent);
    transferInline(balanceAdmin, from, as, "");

    auto toMofify = mt.find(from);
    if(mortIte.mortgegelist.size() ==0 ){
        mt.erase(toMofify);
    }else{
        mt.modify(toMofify, from, [&](auto & s){
            s.mortgegelist = mortIte.mortgegelist;
        });
    }
}

//weight=balance(oct)*(now()-lastvotetime)
//voter account is server account
//@abi action
uint32_t OracleMarket::vote(account_name voted, account_name voter, int64_t weight, uint64_t status){
    require_auth(voter);

    ContractInfo conInfo(_self, voter);
    auto serverIte = conInfo.find(SCORES_INDEX);
    eosio_assert(serverIte != conInfo.end(), CONTRACT_NOT_REGISTER_STILL);

    Mortgaged mt(_self, voted);
    eosio_assert(mt.find(voted) != mt.end(), CANNOT_VOTE_SOMEONE_BEFORE_YOUR_CONTRACT_REGISTERED_ON_MARKET);

    auto iteM = mt.get(voted);

    for(auto ite = iteM.mortgegelist.begin(); ite != iteM.mortgegelist.end(); ite++){
          mortgagepair obj = *ite;
          if(ite->server == voter){
              if(obj.status==STATUS_MORTGAGE_PAIR_CANNOT_FREEZE)
              {
                   obj.status = status;
                   mt.modify(iteM, voter, [&](auto &s){});

                   UserScores userScores(_self, voted);
                   auto votedUser = userScores.find(voted);
                   if(votedUser == userScores.end()){
                       userScores.emplace(voter, [&](auto &s){
                            s.owner = voted;
                            s.scorescnt = weight;
                       });
                   }else{
                       userScores.modify(votedUser, voter, [&](auto &s){
                            s.scorescnt = votedUser->scorescnt + weight;
                       });
                   }
              }
              return status;
          }
    }
    return STATUS_VOTED_ALREADY;
}

uint64_t OracleMarket::getEvilCountBySetStatus(account_name name){
    BehaviorScores bs(_self, dataAdmin);
    auto secondary_index = bs.template get_index<N(bysecondary)>();
    auto ite = secondary_index.begin();

    uint32_t count = 0;
    while(ite!=secondary_index.end()){
        if(ite->status == STATUS_VOTED_EVIL || ite->status == STATUS_APPEALED || ite->status ==STATUS_APPEALED_CHECKED_EVIL){
            count++;

            bs.modify(*ite, 0, [&](auto &s){
                s.status = STATUS_DEALED;
            });
        }
        ite++;
    }

    return count;
}
//@abi action
void OracleMarket::evilbehavior(account_name server, account_name user, std::string memo){
    require_auth(server);

    BehaviorScores bs(_self, dataAdmin);
    uint64_t idFrom = 0;
    if(bs.rbegin()!=bs.rend()){
        idFrom = bs.rbegin()->id+1;
    }

    eosio_assert(STATUS_VOTED_ALREADY != vote(user, server, evilbehscoRate, STATUS_VOTED_EVIL), YOU_VOTED_REPEAT);

    bs.emplace(server, [&](auto &s){
        s.id = idFrom;
        s.server = server;
        s.user = user;
        s.memo = memo;
        s.status = STATUS_VOTED_EVIL;
        s.appealmemo = "";
        s.justicememo = "";
    });
}

//@abi action
void OracleMarket::appealgood(account_name user, uint64_t idevilbeha, std::string memo){
       require_auth(user);
       BehaviorScores bs(_self, dataAdmin);
       auto bsIte = bs.find(idevilbeha);

       eosio_assert(bsIte != bs.end(), APPEALED_BEHAVIOR_NOT_EXIST);
       eosio_assert(bsIte->status < STATUS_APPEALED, APPEALED_OR_CHECKED_BY_ADMIN);

       bs.modify(*bsIte, user, [&](auto &s){
           s.status = STATUS_APPEALED;
           s.appealmemo = memo;
       });
}

//@abi action
void OracleMarket::admincheck(account_name admin, uint64_t idevilbeha, std::string memo, uint8_t status){
     require_auth(admin);

     BehaviorScores bs(_self, dataAdmin);
     auto bsIte = bs.find(idevilbeha);

     eosio_assert(bsIte != bs.end(), APPEALED_BEHAVIOR_NOT_EXIST);
     eosio_assert(bsIte->status < STATUS_APPEALED_CHECKED_GOOD, APPEALED_OR_CHECKED_BY_ADMIN);
     eosio_assert(status== STATUS_APPEALED_CHECKED_GOOD || status == STATUS_APPEALED_CHECKED_EVIL || status == STATUS_APPEALED_CHECKED_UNKNOWN,  NOT_INT_CHECKED_STATUS);

     bs.modify(*bsIte, admin, [&](auto &s){
         s.status = status;
         s.justicememo = memo;
     });
}


//@abi action
void OracleMarket::setconscolim(account_name conadm, uint64_t assfrosec,  uint64_t scores, asset fee){//set contract call, minimum scores required
    require_auth(conadm);

    ContractInfo conInfo(_self, conadm);
    eosio_assert(fee.amount>=0, ASSFROSEC_SCORES_FEE);

    auto ciItem = conInfo.find(SCORES_INDEX);
    if(ciItem != conInfo.end()){
        conInfo.modify(ciItem, conadm, [&](auto &s){
            s.assfrosec = assfrosec;
            s.serverindex = 0;
            s.scores = scores;
            s.fee = fee;
        });
    }else{
        conInfo.emplace(conadm, [&](auto &s){
           s.assfrosec = assfrosec;
           s.serverindex = 0;
           s.scores = scores;
           s.fee = fee;
        });
    }
}

void OracleMarket::clear(account_name scope){
    //int32_t db_end_i64(account_name code, account_name scope, table_name table);
    int32_t ite = db_find_i64(_self, scope, N(contractinfo), 0);
    uint64_t prim = 0;
    int32_t itr_prev = ite;
    eosio_assert(itr_prev>0, "must > 0");
    while (itr_prev>0) {
        itr_prev = db_previous_i64(itr_prev, &prim);
        db_idx64_remove(itr_prev);
    }
}

