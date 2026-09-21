#include "../src/core.h"
#include <iostream>
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main() {
    try {
        ad::Config c{L"SITE",L"000",3};
        auto inventory=ad::classify({{L"sitePC000000",L"zero.example"},{L"SITEPC000002",L"pc.example"},
            {L"SITENB000003",L""},{L"SITEVM000001",L""},{L"OTHERPC000003",L""},{L"SITEPC00012x",L""}},c);
        require(inventory.families.size()==3,"Dynamic families missing");
        require(inventory.unmatched.size()==2,"Invalid names accepted");
        auto rows=ad::report(inventory,c,{});
        require(rows.size()==9,"Automatic ranges incorrect");
        auto pc0=std::find_if(rows.begin(),rows.end(),[](const auto& r){return r.family==L"PC" && r.number==0;});
        require(pc0!=rows.end() && pc0->inAD,"Registered zero falsely marked available");
        auto pc1=std::find_if(rows.begin(),rows.end(),[](const auto& r){return r.name==L"SITEPC000001";});
        require(pc1!=rows.end() && !pc1->inAD,"Gap missing");
        require(pc0->dns==L"zero.example","FQDN lost");
        require(ad::report(inventory,c,std::make_pair(2,2)).size()==3,"Inclusive manual range incorrect");
        bool rejected=false; try { ad::report(inventory,c,std::make_pair(4,3)); } catch(...) { rejected=true; }
        require(rejected,"Reversed range accepted");
        rejected=false; try { ad::report(inventory,c,std::make_pair(0,1000)); } catch(...) { rejected=true; }
        require(rejected,"Overflow range accepted");
        ad::Config wide{L"SITE",L"",6}; auto large=ad::classify({{L"SITEPC999999",L""}},wide);
        rejected=false; try { ad::report(large,wide,{}); } catch(...) { rejected=true; }
        require(rejected,"Unbounded report allocation");
        ad::Row conflict; conflict.network=ad::Network::Responding;
        require(ad::networkLabel(conflict).find(L"Conflicto")!=std::wstring::npos,"Missing conflict");
        conflict.inAD=true; require(ad::networkLabel(conflict)==L"Responde","AD occupancy changed by ping");
        require(ad::parse(L"SITEPC000999",c)->second==999,"Upper bound rejected");
        std::cout << "All core checks passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
