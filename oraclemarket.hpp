/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */
#include <eosiolib/eosio.hpp>
#include <eosiolib/db.h>
#include <eosiolib/asset.hpp>
#include<eosiolib/serialize.hpp>

#include "../EOSDACToken/eosdactoken.hpp"

#include "./tool.hpp"







#define STATUS_MORTGAGE_PAIR_CANNOT_FREEZE 0
#define STATUS_MORTGAGE_PAIR_CAN_FREEZE 1

#define STATUS_NOT_VOTED 0
#define STATUS_VOTED 1
struct mortgagepair{
    mortgagepair(){

    }

    mortgagepair(
    account_name serverpar,
    asset       quantitypar,
    uint64_t     createtimepar,
    uint64_t     timesecfrozenpar){
        this->createtime = createtimepar;
        this->server = serverpar;
        this->quantity = quantitypar;
        this->timesecfrozen = timesecfrozenpar;
        this->status = STATUS_MORTGAGE_PAIR_CANNOT_FREEZE;
        this->bvoted = STATUS_NOT_VOTED;
    }

    account_name server;
    asset       quantity;
    uint64_t    createtime;
    uint64_t    timesecfrozen;
    uint64_t    status;
    uint8_t     bvoted;


    EOSLIB_SERIALIZE( mortgagepair, (server)(quantity)(createtime)(timesecfrozen)(status)(bvoted))
};

struct mortgaged{
    account_name from;
    std::vector<mortgagepair> mortgegelist;

    mortgaged(){

    }
    mortgaged(account_name frompar,
    std::vector<mortgagepair> mortgegelistpar){
        this->from = frompar;
        this->mortgegelist = mortgegelistpar;
    }

    account_name primary_key()const { return from;}
    EOSLIB_SERIALIZE( mortgaged, (from)(mortgegelist))
};

typedef eosio::multi_index<N(mortgaged), mortgaged> Mortgaged;

#define STATUS_UNKNOWN_HEHAVIOR 0
#define STATUS_BAD_BEHAVIOR 1
#define STATUS_GOOD_BEHAVIOR 2

struct scores{
    scores(){}

    scores(account_name ownerpar,
    int64_t scorescntpar){
        this->owner = ownerpar;
        this->scorescnt = scorescntpar;
    }

    account_name owner;
    int64_t scorescnt;
    account_name primary_key()const { return owner;}
    EOSLIB_SERIALIZE( scores, (owner)(scorescnt));
};


struct contractinfo{
    account_name owner;
    int64_t scorescnt;
    int64_t assfrosec;//asset frozen seconds

    account_name primary_key()const { return owner;}
    EOSLIB_SERIALIZE( contractinfo, (owner)(scorescnt)(assfrosec));
};

struct behaviorscores{
    uint64_t id;
    account_name server;
    account_name from;
    std::string  memo;

    uint64_t    status;
    std::string appealmemo;
    std::string justicememo;

    uint64_t primary_key()const { return id;}
    EOSLIB_SERIALIZE( behaviorscores, (id)(server)(from)(memo)(status)(appealmemo)(justicememo));
};


typedef eosio::multi_index<N(userscores), scores> UserScores;
typedef eosio::multi_index<N(scoreslimit), contractinfo> ContractInfo;//Invoking the contract, the minimum required score,default is zero
typedef eosio::multi_index<N(behaviorscores), behaviorscores> BehaviorScores;




class OracleMarket : public eosio::contract{

public:
    OracleMarket( account_name self)
         :contract(self){
        balanceAdmin = currentAdmin;
    }

    account_name balanceAdmin;


    //@abi action
    void mortgage(account_name from, account_name server, const asset &quantity){
        require_auth(from);

        transferfromact tf(from, server, quantity);
        transferFromInline(tf);
        Mortgaged mt(_self, from);

        ContractInfo userScores(_self, server);
        auto serverIte = userScores.find(server);
        eosio_assert(serverIte != userScores.end(), CONTRACT_NOT_REGISTER_STILL);


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
    void unfrosse(account_name server, account_name from, const asset & quantity){
        require_auth(server);

        Mortgaged mt(_self, from);
        auto mortIte = mt.find(from);
        eosio_assert(mortIte!=mt.end(), USER_NOT_MORTGAGED_CANNOT_RELEASE);


        //find first
        for(auto ite = mortIte->mortgegelist.begin();ite != mortIte->mortgegelist.end(); ite++){
              mortgagepair mi = *ite;
              if(quantity == mi.quantity){
                  mi.status = STATUS_MORTGAGE_PAIR_CAN_FREEZE;
                  break;
              }
        }

        mt.modify(*mortIte, server, [&](auto & s){
        });
    }

    //@abi action
    void withdrawfro(account_name from){//withdrawfrozened
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

        if(mortIte.mortgegelist.size() ==0){
            mt.erase(mortIte);
        }else{
            mt.modify(mortIte, from, [&](auto & s){
            });
        }
    }

    //weight=balance(oct)*(now()-lastvotetime)
    //voter account is server account
    //@abi action
    void vote(account_name voted, account_name voter, int64_t weight){
        require_auth(voter);

        ContractInfo userScores(_self, voter);
        auto serverIte = userScores.find(voter);
        eosio_assert(serverIte != userScores.end(), CONTRACT_NOT_REGISTER_STILL);

        Mortgaged mt(_self, voted);

        eosio_assert(mt.find(voted) != mt.end(), CANNOT_VOTE_SOMEONE_NOT_JOIN_OI);

        auto iteM = mt.get(voted);

        for(auto ite = iteM.mortgegelist.begin(); ite != iteM.mortgegelist.end(); ite++){
              mortgagepair obj = *ite;
              if(ite->server == voter && obj.bvoted==STATUS_NOT_VOTED){
                   obj.bvoted = STATUS_VOTED;

                   mt.modify(iteM, voter, [&](auto &s){ });

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
                   break;
              }
        }
    }

    //@abi action
    void evilbehavior(account_name server, account_name from, std::string memo);

    //@abi action
    void appealgood(account_name server, uint64_t idevilbeha, std::string memo);

    //@abi action
    void settogood(account_name admin, uint64_t idevilbeha, std::string memo);

    //@abi action
    void settoevil(account_name admin, uint64_t idevilbeha, std::string memo);

    //@abi action
    void setconscolim(account_name conadm, uint64_t scores);//set contract call, minimum scores required


    const uint64_t normalServerScoresRate = 1;//Provide a normal service and get extra points
    const uint64_t appealAsGoodScoresExtraRate = 1;

    const uint64_t evilBehaviorScoresRate = 10;//Evil offensive score, It is evil to abuse others.

    const uint64_t minEvilVoteTimeIntervar = 24*60*60;//The minimum time interval for bad votes
    const uint64_t finaltimeSecFrozen = 2*24*60*60;//mortgage freeze time in seconds
};
/*
OI server need frozen time interface
*/

EOSIO_ABI( OracleMarket, (mortgage)(unfrosse)(withdrawfro)(vote)(evilbehavior)(appealgood)(settogood)(settoevil)(setconscolim))


