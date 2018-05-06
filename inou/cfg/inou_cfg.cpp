
#include <fstream>
#include "inou_cfg.hpp"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stack>
#include "lgraphbase.hpp"
#include "lgedgeiter.hpp"
using std::string;
Inou_cfg_options_pack::Inou_cfg_options_pack() {

	Options::get_desc()->add_options()
					("cfg_output,o", boost::program_options::value(&cfg_output), "cfg output <filename> for graph")
					("cfg_input,i", boost::program_options::value(&cfg_input), "cfg input <filename> for graph");

	boost::program_options::variables_map vm;
	boost::program_options::store(
					boost::program_options::command_line_parser(Options::get_cargc(), Options::get_cargv()).options(
									*Options::get_desc()).allow_unregistered().run(), vm);
	if (vm.count("cfg_output")) {
		cfg_output = vm["cfg_output"].as<string>();
	}
	else {
		cfg_output = "output.cfg";
	}

	if (vm.count("cfg_input")) {
		cfg_input = vm["cfg_input"].as<string>();
	}
	else {
		cfg_input = "test.cfg";
	}

	console->info("inou_cfg cfg_output:{} cfg_input:{} graph_name:{}", cfg_output, cfg_input, graph_name);
}

Inou_cfg::Inou_cfg() {
}

Inou_cfg::~Inou_cfg() {
}

std::vector<LGraph *> Inou_cfg::generate() {

	std::vector<LGraph *> lgs;
	if (opack.graph_name != "") {
		lgs.push_back(new LGraph(opack.lgdb_path, opack.graph_name, false)); // Do not clear
		// No need to sync because it is a reload. Already sync
	}
	else {
		assert(opack.cfg_input != "");

		lgs.push_back(new LGraph(opack.lgdb_path));
		string cfg_file = opack.cfg_input;

    int fd = open(cfg_file.c_str(), O_RDONLY);
		if (fd<0) {
      console->error("cannot find input file\n");
			exit(-3);
		}

	  struct stat sb;
	  fstat(fd, &sb);
	  //printf("Size: %lu\n", (uint64_t)sb.st_size);

	  char* memblock = (char* )mmap(nullptr, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
	  if (memblock == MAP_FAILED) {
      console->error("error, mmap failed\n");
	  	exit(-3);
	  }

    //cfg_2_lgraph_bk(&memblock, lgs[0]);
    cfg_2_lgraph(&memblock,lgs);;

		for(int i = 0; i< lgs.size(); i++){
			lgs[i]->sync();
		}
    close(fd);
	}
	return lgs;
}

void Inou_cfg::cfg_2_lgraph(char** memblock, std::vector<LGraph*>& lgs){
	string s;
	std::map <string,Index_ID> name2id;
	std::vector<Index_ID> id_nbr_null;//these id's neighbor is a null
	int64_t nid_final = 0;

	std::map<string, std::vector<string >>  vecs;

	char *p = strtok(*memblock, "\n\r\f");
	LGraph* g = lgs[0];

	while(p){
		std::vector <string> words = split(p);

		if(*(words.begin()) == "END")
			break;

		string w1st = *(words.begin());
		string w2nd = *(words.begin()+1);
    string w3rd = *(words.begin()+2);
		string w5th = *(words.begin()+4);
		string w6th = *(words.begin()+5);
		string w7th = *(words.begin()+6);
		string w8th;
		string w9th;
		if(w5th == "if" || w5th == "::{")
			w8th = *(words.begin()+7);

		if(w5th == "if")
			w9th = *(words.begin()+8);

		string dfg_data = p;

		fmt::print("dfg_data:{}\n", dfg_data);


		//if(w3rd != "0" && std::stoi(w3rd)>= lgs.size()){//create sub graph if different scope id
		//	lgs.push_back(new LGraph(opack.lgdb_path));

		//}
		//else if(w3rd != "0"){//construct sub-graph, i.e. function definition
		//	g_construct(words, lgs[std::stoi(w3rd)]);
		//}
		//else{
		//	g_construct(words, gtop);
		//}

		/*
      I.process 1st node
      only assign node type for first K in every line of cfg
    */
		if(name2id.count(w1st) == 0){//if node has not been created before
			Node new_node = g->create_node();
			name2id[w1st] = new_node.get_nid();
			nid_final =  new_node.get_nid();//keep update the latest final nid

			fmt::print("create node:{}, nid:{}\n", w1st, name2id[w1st]);

			g->set_node_wirename(new_node.get_nid(), dfg_data.c_str());

			if(     w5th == ".()")
				g->node_type_set(name2id[w1st], CfgFunctionCall_Op);
			else if(w5th == "for")
				g->node_type_set(name2id[w1st], CfgFor_Op);
			else if(w5th == "while")
				g->node_type_set(name2id[w1st], CfgWhile_Op);
			else
				g->node_type_set(name2id[w1st], CfgAssign_Op);
		}
		else{
			g->set_node_wirename(name2id[w1st], dfg_data.c_str());

			if     (w5th == ".()")
				g->node_type_set(name2id[w1st], CfgFunctionCall_Op);
			else if(w5th == "for")
				g->node_type_set(name2id[w1st], CfgFor_Op);
			else if(w5th == "while")
				g->node_type_set(name2id[w1st], CfgWhile_Op);
			else if(w5th == "if")
				g->node_type_set(name2id[w1st], CfgIf_Op);
			else
				g->node_type_set(name2id[w1st], CfgAssign_Op);
		}

		/*
      II-0.process 2nd node and 9th node(if-else merging node)
    */

		if(w5th == "if" && name2id.count(w9th) == 0){//if node has not been created before
			Node new_node = g->create_node();
			name2id[w9th] = new_node.get_nid();
			nid_final =  new_node.get_nid();//keep update the latest final nid
			fmt::print("create node:{}, nid:{}\n", w9th, name2id[w9th]);

			g->node_type_set(name2id[w9th], CfgIfMerge_Op);
		}

		if(w2nd != "null" && name2id.count(w2nd) == 0){
			Node new_node = g->create_node();
			name2id[w2nd] = new_node.get_nid();
			nid_final =  new_node.get_nid();//keep update the latest final nid
			fmt::print("create node:{}, nid:{}\n", w2nd, name2id[w2nd]);
		}


		/*
      III.deal with edge connection
    */
		Index_ID src_nid, dst_nid;

		if(w5th == "if"){//special case: if
			//III-1. connect if node to the begin of "true chunk" statement
			src_nid = name2id[w1st];
			dst_nid = name2id[w7th];
			g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
			fmt::print("if statement, connect src_node {} to dst_node {} ----- 1\n", src_nid, dst_nid);

			//III-2. connect if node to the begin of "false chunk" statement
			if(w8th != "null"){
				src_nid = name2id[w1st];
				dst_nid = name2id[w8th];
				g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
				fmt::print("if statement, connect src_node {} to dst_node {} ----- 2\n", src_nid, dst_nid);
			}

			//III-3. connect top of stack to end if node
      if(vecs[w7th].back() != w9th){
				src_nid = name2id[vecs[w7th].back()];
				dst_nid = name2id[w9th];
				g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
				fmt::print("if statement, connect src_node {} to dst_node {} ----- 3\n", src_nid, dst_nid);
      }
			fmt::print("Hello~~~\n");

			if(w8th != "null"){
        if(vecs[w8th].back() != w9th ){
					src_nid = name2id[vecs[w8th].back()];
					dst_nid = name2id[w9th];
					g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
					fmt::print("if statement, connect src_node {} to dst_node {} ----- 4\n", src_nid, dst_nid);
				}
			}
			else{//only one branch of if. EX.  K13  K30   63  169  if   tmp3  K15  null 'K13
				src_nid = name2id[w1st];
				dst_nid = name2id[w9th];
				g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
				fmt::print("if statement, connect src_node {} to dst_node {} ----- 5\n", src_nid, dst_nid);
			}


			//III-4. if it is an outer if statement, link w9th to w2nd
			if(w2nd != "null"){
				src_nid = name2id[w9th];
				dst_nid = name2id[w2nd];
				g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
				fmt::print("if statement, connect src_node {} to dst_node {} ----- 6\n", src_nid, dst_nid);
			}

			//III-5. figure out which stack w1st is belong to and push w9th into that stack
			for (auto const& x : vecs){
				if(w1st == x.second.back()){
					vecs[x.second[0]].push_back(w9th);
					break;
				}
			}

		}//end special case: if

		else if(w5th == "for"){
			//I. True: connect for node to body
			src_nid = name2id[w1st];
			dst_nid = name2id[w7th];
			g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
			fmt::print("for statement, connect src_node {} to dst_node {}\n", src_nid, dst_nid);
			//II. False: connect for node to next event
			src_nid = name2id[w1st];
			dst_nid = name2id[w2nd];
			g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
			fmt::print("for statement, connect src_node {} to dst_node {}\n", src_nid, dst_nid);
		}//end special case: for

		else if(w5th == "while"){
			//I. connect while node to body
			src_nid = name2id[w1st];
			dst_nid = name2id[w7th];
			g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
			fmt::print("while statement, connect src_node {} to dst_node {}\n", src_nid, dst_nid);

			//II. False: connect while node to next event
			src_nid = name2id[w1st];
			dst_nid = name2id[w2nd];
			g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
			fmt::print("while statement, connect src_node {} to dst_node {}\n", src_nid, dst_nid);
		}//end special case: while

		/*
		else if(w5th == "::{"){
			//connect to the begin of function call
			src_nid = name2id[w1st];
			dst_nid = name2id[w8th];
			g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
			fmt::print("function call statement, connect src_node {} to dst_node {} ----- 1\n", src_nid, dst_nid);

			// connect end of function call body
			for(auto i = 0; i< id_nbr_null.size(); i++){
				fmt::print("neighbor is null{}\n",id_nbr_null[i]);
				src_nid = *(id_nbr_null.begin()+i);
				dst_nid = name2id[w2nd];
				g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, i, true));
				fmt::print("function call statement, connect src_node {} to dst_node {} ----- 4\n", src_nid, dst_nid);
			}
		}//end special case: ::{
		*/
		else if(w2nd == "null"){ //no w2nd to create edge, only update vecs
			bool belong_tops = false;
			string target_vec_id = w1st;

			//check equivalence between src_nid and every top of vecs
			for (auto const& x : vecs){
				if(w1st == x.second.back()){
					belong_tops = true;
					target_vec_id = x.first;
					break;
				}
			}
			if(!belong_tops)
				vecs[target_vec_id].push_back(w1st);

		}
		else if(w2nd != "null"){ //normal edge connection: Kx->Ky, update vecs
			src_nid = name2id[w1st];
			dst_nid = name2id[w2nd];
			g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
			fmt::print("normal case connection, connect src_node {} to dst_node {} ----- 5\n", src_nid, dst_nid);

			bool belong_tops = false;
			string target_vec_id = w1st;

			//check equivalence between src_nid and every top of vecs
			for (auto const& x : vecs){
				if(w1st == x.second.back()){
					belong_tops = true;
					target_vec_id = x.first;
					break;
				}
			}

			if(!belong_tops){
				vecs[target_vec_id].push_back(w1st);
				vecs[target_vec_id].push_back(w2nd);
			}
			else
				vecs[target_vec_id].push_back(w2nd);
		}


		fmt::print("\n");
		p = strtok(nullptr, "\n\r\f");
	}//end while loop

	/*
    deal with GIO
  */
	//Graph input
	Node gio_node_bg = g->create_node();
	fmt::print("create node:{}, nid:{}\n", "GIO", gio_node_bg.get_nid());
	g->node_type_set(gio_node_bg.get_nid(), GraphIO_Op);
	Index_ID src_nid = gio_node_bg.get_nid();
	Index_ID dst_nid = name2id["K1"];
	g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));

	//Graph output
	Node gio_node_ed = g->create_node();
	fmt::print("create node:{}, nid:{}\n", "GIO", gio_node_ed.get_nid());
	g->node_type_set(gio_node_ed.get_nid(), GraphIO_Op);
	src_nid = nid_final;
	fmt::print("total node number:{}\n", name2id.size());
	dst_nid = gio_node_ed.get_nid();;
	g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));


	for (auto& x : vecs){
		fmt::print("\nvid {} content is\n",x.first);
		while(!x.second.empty()){
			fmt::print("{}\n",x.second.back());
			x.second.pop_back();
		}
	}


}

/*
 * create lgraph based on every first two nodes defined in CFG table
 */

void Inou_cfg::cfg_2_lgraph_bk(char** memblock, LGraph* g){
		string s;
		std::vector<string> id2name(1);
		std::map <string,Index_ID> name2id;
    std::vector<Index_ID> id_nbr_null;//these id's neighbor is a null
    int64_t nid_final = 0;
    char *p = strtok(*memblock, "\n\r\f");

		while(p){
      std::vector <string> words = split(p);

			if(*(words.begin()) == "END")
				break;

			string w1st = *(words.begin());
			string w2nd = *(words.begin()+1);
			string w5th = *(words.begin()+4);
			string w6th = *(words.begin()+5);
			string w7th = *(words.begin()+6);
			string w8th;
			if(w5th == "if" || w5th == "::{")
      	w8th = *(words.begin()+7);


      string dfg_data = p;

      fmt::print("dfg_data:{}\n", dfg_data);

      /*
			  I.process 1st node
        only assign node type for first K in every line of cfg
      */
			if(name2id.count(w1st) == 0){//if node has not been created before
				Node new_node = g->create_node();
				name2id[w1st] = new_node.get_nid();
        nid_final =  new_node.get_nid();//keep update the latest final nid

        fmt::print("create node:{}, nid:{}\n", w1st, name2id[w1st]);

        g->set_node_wirename(new_node.get_nid(), dfg_data.c_str());

				if(     w5th == ".()")
					g->node_type_set(name2id[w1st], CfgFunctionCall_Op);
				else if(w5th == "for")
          g->node_type_set(name2id[w1st], CfgFor_Op);
				else if(w5th == "while")
          g->node_type_set(name2id[w1st], CfgWhile_Op);
				else
          g->node_type_set(name2id[w1st], CfgAssign_Op);
			}
			else{
        g->set_node_wirename(name2id[w1st], dfg_data.c_str());

        if     (w5th == ".()")
          g->node_type_set(name2id[w1st], CfgFunctionCall_Op);
        else if(w5th == "for")
          g->node_type_set(name2id[w1st], CfgFor_Op);
        else if(w5th == "while")
          g->node_type_set(name2id[w1st], CfgWhile_Op);
        else if(w5th == "if")
          g->node_type_set(name2id[w1st], CfgIf_Op);
        else
          g->node_type_set(name2id[w1st], CfgAssign_Op);
      }

      /*
        II.process 2nd node
      */

			if(w2nd != "null" && name2id.count(w2nd) == 0){
				Node new_node = g->create_node();
				name2id[w2nd] = new_node.get_nid();
        nid_final =  new_node.get_nid();//keep update the latest final nid
        fmt::print("create node:{}, nid:{}\n", w2nd, name2id[w2nd]);
			}
			else if(w2nd == "null"){
				if(w5th == "if" && !w8th.empty() && w8th != "null")
					;
				else
					id_nbr_null.push_back(name2id[w1st]);
			}


      /*
        III.deal with edge connection
      */
			Index_ID src_nid, dst_nid;

			if(w5th == "if"){//special case: if
        //III-1. connect if node to the begin of "true chunk" statement
				src_nid = name2id[w1st];
				dst_nid = name2id[w7th];
        g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
				fmt::print("if statement, connect src_node {} to dst_node {} ----- 1\n", src_nid, dst_nid);

        //III-2. connect if node to the begin of "false chunk" statement
        if(w8th != "null"){
          src_nid = name2id[w1st];
          dst_nid = name2id[w8th];
          g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
					fmt::print("if statement, connect src_node {} to dst_node {} ----- 2\n", src_nid, dst_nid);
        }

        if(w2nd != "null" && w8th =="null"){//outer if
          src_nid = name2id[w1st];
          dst_nid = name2id[w2nd];
          g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
					fmt::print("if statement, connect src_node {} to dst_node {} ----- 3\n", src_nid, dst_nid);
        }

        //III-3. connect the end of "every chunk" statement to the "end if" node
        if(w2nd !="null"){// it is the final if
					for(auto i = 0; i< id_nbr_null.size(); i++){
						fmt::print("con{}\n",id_nbr_null[i]);
						src_nid = *(id_nbr_null.begin()+i);
						dst_nid = name2id[w2nd];
						g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, i, true));
						fmt::print("if statement, connect src_node {} to dst_node {} ----- 4\n", src_nid, dst_nid);
					}
          id_nbr_null.clear();
        }
			}//end special case: if

			else if(w5th == "for"){
				//I. connect for node to body
				src_nid = name2id[w1st];
				dst_nid = name2id[w7th];
				g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
				fmt::print("for statement, connect src_node {} to dst_node {} ----- 1\n", src_nid, dst_nid);

				id_nbr_null.clear();

				//III connect for begin node to for end node
				src_nid = name2id[w1st];
				dst_nid = name2id[w2nd];
				g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
				fmt::print("for statement, connect src_node {} to dst_node {} ----- 3\n", src_nid, dst_nid);
			}//end special case: for

			else if(w5th == "while"){

				//I. connect for node to body
				src_nid = name2id[w1st];
				dst_nid = name2id[w7th];
				g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
				fmt::print("while statement, connect src_node {} to dst_node {} ----- 1\n", src_nid, dst_nid);

				id_nbr_null.clear();

				//III connect while begin node to while end node
				src_nid = name2id[w1st];
				dst_nid = name2id[w2nd];
				g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
				fmt::print("while statement, connect src_node {} to dst_node {} ----- 3\n", src_nid, dst_nid);
      }//end special case: while

			else if(w5th == "::{"){
        //connect to the begin of function call
        src_nid = name2id[w1st];
        dst_nid = name2id[w8th];
        g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
        fmt::print("function call statement, connect src_node {} to dst_node {} ----- 1\n", src_nid, dst_nid);

        // connect end of function call body
        for(auto i = 0; i< id_nbr_null.size(); i++){
          fmt::print("neighbor is null{}\n",id_nbr_null[i]);
          src_nid = *(id_nbr_null.begin()+i);
          dst_nid = name2id[w2nd];
          g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, i, true));
          fmt::print("function call statement, connect src_node {} to dst_node {} ----- 4\n", src_nid, dst_nid);
        }
      }//end special case: ::{

			else if(w2nd != "null"){ //normal case:Kx->Ky
				src_nid = name2id[w1st];
				dst_nid = name2id[w2nd];
        g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
				fmt::print("normal case connection, connect src_node {} to dst_node {} ----- 5\n", src_nid, dst_nid);
				//fmt::print("src_node:{}, dst_node:{}\n",id2name[src_nid],id2name[dst_nid]);
			}
			fmt::print("\n");
      p = strtok(nullptr, "\n\r\f");
		}//end while loop

    /*
      deal with GIO
    */
    //Graph input
    Node gio_node_bg = g->create_node();
    fmt::print("create node:{}, nid:{}\n", "GIO", gio_node_bg.get_nid());
    g->node_type_set(gio_node_bg.get_nid(), GraphIO_Op);
    Index_ID src_nid = gio_node_bg.get_nid();
    Index_ID dst_nid = name2id["K1"];
    g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));

    //Graph output
    Node gio_node_ed = g->create_node();
    fmt::print("create node:{}, nid:{}\n", "GIO", gio_node_ed.get_nid());
    g->node_type_set(gio_node_ed.get_nid(), GraphIO_Op);
    src_nid = nid_final;
    fmt::print("total node number:{}\n", name2id.size());
    dst_nid = gio_node_ed.get_nid();;
    g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
}


std::vector<string> Inou_cfg::split(const string& str){
	typedef string::const_iterator iter;
	std::vector<string> ret;

	iter i = str.begin();
	while(i != str.end()){
		i = find_if(i, str.end(), not_space);
    // find end of next word
		iter j = find_if(i, str.end(), space);

		//copy the characters in [i,j)
		if (i != str.end())
			ret.push_back(string(i,j));

		i = j;
	}
	return ret;
}

void Inou_cfg::lgraph_2_cfg(const LGraph* g, const string& filename ) {
  int line_cnt = 0;
  for (auto &idx : g->fast()) {
    if (g->get_node_wirename(idx) != nullptr) {
      fmt::print("{}\n",g->get_node_wirename(idx));//for now, just print out cfg, maybe mmap write later
      ++line_cnt;
    }
	}
  fmt::print("END\n");
  ++line_cnt;
  fmt::print("line_cnt = {}\n",line_cnt);
}

void Inou_cfg::generate(std::vector<const LGraph *> out) {
  if (out.size() == 1) {
    lgraph_2_cfg(out[0], opack.cfg_output);
  }
  else {
    for (const auto &g : out) {
      string file = g->get_name() + "_" + opack.cfg_output;
      lgraph_2_cfg(g, file);
    }
  }
}

//tmp zone
//str4num_1 = (*(words.begin())).substr(1);//get substring s[1:end]
//int num1 = std::stoi(str4num_1);
//fmt::print("num1 number is {}\n", num1);

//str4num_2 = (*(words.begin()+1)).substr(1);//get substring s[1:end]
//int num2 = std::stoi(str4num_2);
//fmt::print("num2 number is {}\n", num2);

//for(std::vector<string>::size_type i = 0; i != words.size(); i++){
//  if(i == words.size()-1)
//    dfg_data = dfg_data + words[i];//no final space stored
//  else
//    dfg_data = dfg_data + words[i] + " ";
//}
/*
 * no need to care about target of if-end statement inside a loop
 * deal with it later in DFG
//II. connect body end node to for end node
for(auto i = 0; i< id_nbr_null.size(); i++){
  fmt::print("con{}\n",id_nbr_null[i]);
  src_nid = *(id_nbr_null.begin()+i);
  dst_nid = name2id[w1st];//modify from w2nd to w1st
  g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, i, true));
  fmt::print("for statement, connect src_node {} to dst_node {} ----- 2\n", src_nid, dst_nid);
}
*/

/*
else if(w2nd != "null"){ //normal case:Kx->Ky, update stacks
src_nid = name2id[w1st];
dst_nid = name2id[w2nd];
g->add_edge (Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
fmt::print("normal case connection, connect src_node {} to dst_node {} ----- 5\n", src_nid, dst_nid);

bool belong_tops = false;
Index_ID target_stack_id = src_nid;

//check equivalence bt src_nid and every top of stacks
for (auto const& x : stacks){
if(src_nid == x.second.top()){
belong_tops = true;
target_stack_id = x.first;
break;
}
}

if(!belong_tops){
stacks[target_stack_id].push(src_nid);
stacks[target_stack_id].push(dst_nid);
}
else
stacks[target_stack_id].push(dst_nid);

}
*/








