#include "eosiolib/eosio.hpp"

#include "oraclemarket.hpp"

using namespace eosio;

//@abi action
void OracleMarket::mortgage(account_name from, account_name server, const asset &quantity){
    require_auth(from);

    transferfromact tf(from, server, quantity);
    transferFromInline(tf);

    ContractInfo userScores(_self, server);
    auto serverIte = userScores.find(server);
    eosio_assert(serverIte != userScores.end(), CONTRACT_NOT_REGISTER_STILL);

    Mortgaged mt(_self, from);
    if(mt.find(from) == mt.end()){

        std::vector<mortgagepair> mortgegelist;
        mortgagepair mp(server, quantity, now(), serverIte->assfrosec);
        mortgegelist.push_back(mp);

        mortgaged m(from, mortgegelist);
        mt.emplace(from, [&]( auto& s ) {
            s.from = from;
            s.mortgegelist = mortgegelist;
        });
    }else{

        auto itefrom = mt.get(from);
        mortgagepair mp(server, quantity, now(), serverIte->assfrosec);
        itefrom.mortgegelist.push_back(mp);
        mt.modify(itefrom, from, [&](auto &s){
        });
    }
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

//@abi action
void OracleMarket::withdrawfro(account_name from){//withdrawfrozened
    require_auth(from);

    Mortgaged mt(_self, from);
    eosio_assert(mt.find(from)!=mt.end(), USER_NOT_MORTGAGED_CANNOT_RELEASE);

    auto mortIte = mt.get(from);
    for(auto ite = mortIte.mortgegelist.begin();ite != mortIte.mortgegelist.end(); ite++){
          mortgagepair mi = *ite;
          if(mi.createtime+mi.timesecfrozen<now() || mi.status == STATUS_MORTGAGE_PAIR_CAN_FREEZE){
              mortIte.mortgegelist.erase(ite);
              transferInline(balanceAdmin, from, mi.quantity, "");
          }
    }

    if(mortIte.mortgegelist.size() ==0 ){
        mt.erase(mortIte);
    }else{
        mt.modify(mortIte, from, [&](auto & s){
        });
    }
}

//weight=balance(oct)*(now()-lastvotetime)
//voter account is server account
//@abi action
uint32_t OracleMarket::vote(account_name voted, account_name voter, int64_t weight, uint64_t status){
    require_auth(voter);

    ContractInfo conInfo(_self, voter);
    auto serverIte = conInfo.find(voter);
    eosio_assert(serverIte != conInfo.end(), CONTRACT_NOT_REGISTER_STILL);

    Mortgaged mt(_self, voted);
    eosio_assert(mt.find(voted) != mt.end(), CANNOT_VOTE_SOMEONE_NOT_USE_YOUR_CONTRACT_REGISTERED_ON_MARKET);

    auto iteM = mt.get(voted);

    for(auto ite = iteM.mortgegelist.begin(); ite != iteM.mortgegelist.end(); ite++){
          mortgagepair obj = *ite;
          if(ite->server == voter && obj.bvoted==STATUS_NOT_VOTED){
               obj.bvoted = status;


               mt.modify(iteM, voter, [&](auto &s){});

               UserScores userScores(_self, voted);
               auto votedUser = userScores.find(voted);
               if(votedUser == userScores.end()){
                   userScores.emplace(voter, [&](auto &s){
                        s.owner = voted;
                        s.scorescnt = weight;
                   });
               }else{
                   userScores.modify(*votedUser, voter, [&](auto &s){
                        s.scorescnt = votedUser->scorescnt + weight;
                   });
               }
               return status;
          }
    }
    return STATUS_VOTED_ALREADY;
}

uint64_t OracleMarket::getEvilCount(account_name name){
    BehaviorScores bs(_self, dataAdmin);
    auto secondary_index = bs.template get_index<N(bysecondary)>();


}
//@abi action
void OracleMarket::evilbehavior(account_name server, account_name user, std::string memo){
    require_auth(server);

    BehaviorScores bs(_self, dataAdmin);
    uint64_t idFrom = 0;
    if(bs.rbegin()!=bs.rend()){
        idFrom = bs.rbegin()->id+1;
    }

    eosio_assert(STATUS_VOTED_ALREADY != vote(user, server, evilBehaviorScoresRate, STATUS_VOTED_EVIL), YOU_VOTED_REPEAT);

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
void OracleMarket::setconscolim(account_name conadm, uint64_t scores){//set contract call, minimum scores required
    ContractInfo conInfo(_self, conadm);
    auto ciItem = conInfo.find(conadm);
    if(ciItem != conInfo.end()){
        conInfo.modify(ciItem, conadm, [&](auto &s){
        });
    }else{
        conInfo.emplace(conadm, [&](auto &s){
           s.assfrosec = scores;
           s.serverindex = 0;
        });
    }
}

