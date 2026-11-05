#include "erasgf/library.hpp"
#include <chrono>
#include <iostream>
#include <sys/resource.h>
#include <memory>
using namespace erasgf;
template<class F> void measure(const char* phase, F f) {
 const auto start=std::chrono::steady_clock::now();f();struct rusage usage{};getrusage(RUSAGE_SELF,&usage);
 std::cout<<phase<<"\t"<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<"\t"<<usage.ru_maxrss<<std::endl;
}
int main(int argc,char** argv){try{
 if(argc!=3){std::cerr<<"Usage: library-benchmark STORAGE_ROOT SEPARATE_STATE_DIRECTORY\n";return 2;}
 std::unique_ptr<Library> l; measure("load",[&]{l.reset(new Library(argv[1],argv[2]));});
 measure("refresh",[&]{l->refresh();});std::cout<<"records\t"<<l->records().size()<<std::endl;
#ifdef ERASGF_BENCH_STATS
 std::cout<<"refresh_parsed\t"<<l->refresh_stats().parsed<<"\nrefresh_reused\t"<<l->refresh_stats().reused<<std::endl;
#endif
 const auto expected=l->records().size();
 measure("query_all",[&]{l->query({});});
 LibraryFilter f;f.query="Cho";measure("query_text",[&]{l->query(f);});
 measure("query_cached_100",[&]{for(int i=0;i<100;++i)l->query(f);});
 std::mt19937 rng(1);measure("random_100",[&]{for(int i=0;i<100;++i)l->random_unviewed(rng);});
 if(!l->records().empty()){auto path=l->records().front().path;bool before=l->records().front().viewed;
 measure("viewed_write",[&]{l->set_viewed(path,!before);});l->set_viewed(path,before);}
 l.reset();measure("reload",[&]{l.reset(new Library(argv[1],argv[2]));});
 if (!l->loaded() || l->records().size()!=expected) throw std::runtime_error("Reload lost catalogue records");
 measure("unchanged_refresh",[&]{l->refresh();});
#ifdef ERASGF_BENCH_STATS
 std::cout<<"unchanged_parsed\t"<<l->refresh_stats().parsed<<"\nunchanged_reused\t"<<l->refresh_stats().reused<<std::endl;
#endif
 return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
