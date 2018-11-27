//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lefdef_json.hpp"

// void Tech_file::to_json(){
//  rapidjson::StringBuffer s;
//  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(s);
//
//  for(vector<Tech_layer>::const_iterator iter = layers.begin(); iter != layers.end(); ++iter){
//    iter->to_json(writer);
//  }
//
//  for(vector<Tech_macro>::const_iterator iter = macros.begin(); iter != macros.end(); ++iter){
//    iter->to_json(writer);
//  }
//
//  for(vector<Tech_via>::const_iterator iter = vias.begin(); iter != vias.end(); ++iter){
//    iter->to_json(writer);
//  }
//
//  std::ofstream fout;
//  fout.open("lef.json");
//  if (!fout.is_open()) {
//    std::cerr << "ERROR: could not open json file [" << "lef.json" << "]";
//    exit(-4);
//  }
//  fout << s.GetString() << endl;
//  fout.close();
//}

/*
void Tech_macro::to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer>& writer) const {
  writer.StartObject();
  writer.Key("macro");
  {
    writer.StartObject();
    {
      writer.Key("name");
      writer.String(name.c_str());

      writer.Key("size");
      writer.StartArray();
      for(vector<double>::const_iterator iter = size.begin(); iter != size.end(); ++iter)
        writer.Double(*iter);
      writer.EndArray();

      writer.Key("pins");
      writer.StartObject();
      {// deal with all pins of a macro
        for(vector<Tech_pin>::const_iterator iter_pin = pins.begin(); iter_pin != pins.end(); ++iter_pin){
          writer.Key((*iter_pin).pin_name.c_str());//name of pin
          writer.StartObject();
          {//deal with a single pin
            writer.Key("direction");
            writer.Uint64((*iter_pin).pin_direction);
            writer.Key("use");
            writer.String((*iter_pin).pin_use.c_str());
            writer.Key("ports");
            writer.StartObject();
            {//deal with ports of a single pin
              for(vector<Tech_port>::const_iterator iter_port = (*iter_pin).ports.begin(); iter_port != (*iter_pin).ports.end();
++iter_port){ writer.Key("layer"); writer.String((*iter_port).metal_name.c_str());

                writer.Key("rects");
                  writer.StartObject();//deal with rectangles of a single port
                  for(vector<Tech_rect>::const_iterator iter_rect = (*iter_port).rects.begin(); iter_rect !=
(*iter_port).rects.end(); ++iter_rect){ writer.StartArray(); writer.Double((*iter_rect).xl); writer.Double((*iter_rect).yl);
                    writer.Double((*iter_rect).xh);
                    writer.Double((*iter_rect).yh);
                    writer.EndArray();
                  }
                  writer.EndObject();
              }//end for
            }
            writer.EndObject();
          }
          writer.EndObject();
        }//end for
      }
      writer.EndObject();
    }
    writer.EndObject();
  }
  writer.EndObject();

}
*/

// void Tech_layer::to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer>& writer) const {
//  if(strncmp(name.c_str(), "Metal", 5) ==0) { //Layer type is Metal
//    writer.StartObject();
//    writer.Key("layer");
//    {
//      writer.StartObject();
//      writer.Key("name");
//      writer.String( name.c_str() );
//
//      writer.Key("horizontal");
//      writer.Bool(horizontal);
//
//      writer.Key("minwidth");
//      writer.Double(minwidth);
//
//      writer.Key("area");
//      writer.Double(area);
//
//      writer.Key("pitches");
//      writer.StartArray();
//      for(auto iter = pitches.begin(); iter != pitches.end(); ++iter)
//        writer.Double(*iter);
//      writer.EndArray();
//
//      writer.Key("width");
//      writer.Double(width);
//
//      writer.Key("spacing");
//      writer.Double(spacing);
//
//      writer.Key("spacing_eol");
//      writer.StartArray();
//      for(auto iter = spacing_eol.begin(); iter != spacing_eol.end(); ++iter)
//        writer.Double(*iter);
//      writer.EndArray();
//
//      writer.Key("spctb_prl");
//      writer.Double(spctb_prl);
//
//      writer.Key("spctb_width");
//      writer.StartArray();
//      for(auto iter = spctb_width.begin(); iter != spctb_width.end(); ++iter)
//        writer.Double(*iter);
//      writer.EndArray();
//
//      writer.Key("spctb_spacing");
//      writer.StartArray();
//      for(auto iter = spctb_spacing.begin(); iter != spctb_spacing.end(); ++iter)
//        writer.Double(*iter);
//      writer.EndArray();
//
//      writer.EndObject();
//    }
//    writer.EndObject();
//  }
//
//  else if(strncmp(name.c_str(), "Via", 3) ==0) { //Layer type is Via
//    writer.StartObject();
//    writer.Key("layer");
//    {
//      writer.StartObject();
//      writer.Key("name");
//      writer.String( name.c_str() );
//
//      writer.Key("spacing");
//      writer.Double(spacing);
//
//      writer.Key("width");
//      writer.Double(width);
//      writer.EndObject();
//    }
//    writer.EndObject();
//  }
//}
//
// void Tech_via::to_json(rapidjson::PrettyWriter<rapidjson::StringBuffer>& writer) const {
//  writer.StartObject();
//  writer.Key("via");
//  {
//    writer.StartObject();
//    writer.Key("name");
//    writer.String(name.c_str());
//    writer.Key("layers");
//    {
//      writer.StartObject();
//      for(auto iter_layer = vlayers.begin(); iter_layer != vlayers.end(); ++iter_layer){
//        writer.Key(iter_layer->layer_name.c_str());
//        writer.StartArray();
//        writer.Double(iter_layer->rect.xl);
//        writer.Double(iter_layer->rect.yl);
//        writer.Double(iter_layer->rect.xh);
//        writer.Double(iter_layer->rect.yh);
//        writer.EndArray();
//      }
//      writer.EndObject();
//    }
//    writer.EndObject();
//  }
//  writer.EndObject();
//}
