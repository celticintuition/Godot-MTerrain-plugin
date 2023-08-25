#include "mnavigation_region_3d.h"
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>


#define CHUNK_INFO grid->grid_update_info[grid_index]


void MNavigationRegion3D::_bind_methods(){
    ClassDB::bind_method(D_METHOD("update_navmesh","cam_pos"), &MNavigationRegion3D::update_navmesh);
    ClassDB::bind_method(D_METHOD("_finish_update","nvm"), &MNavigationRegion3D::_finish_update);
    ClassDB::bind_method(D_METHOD("_update_loop"), &MNavigationRegion3D::_update_loop);
    ClassDB::bind_method(D_METHOD("_set_is_updating","input"), &MNavigationRegion3D::_set_is_updating);

    ClassDB::bind_method(D_METHOD("set_npoint_by_pixel","px","py","val"), &MNavigationRegion3D::set_npoint_by_pixel);
    ClassDB::bind_method(D_METHOD("get_npoint_by_pixel","px","py"), &MNavigationRegion3D::get_npoint_by_pixel);
    ClassDB::bind_method(D_METHOD("draw_npoints","brush_pos","radius","add"), &MNavigationRegion3D::draw_npoints);
    ClassDB::bind_method(D_METHOD("set_npoints_visible","val"), &MNavigationRegion3D::set_npoints_visible);


    ClassDB::bind_method(D_METHOD("set_nav_data","input"), &MNavigationRegion3D::set_nav_data);
    ClassDB::bind_method(D_METHOD("get_nav_data"), &MNavigationRegion3D::get_nav_data);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT,"data",PROPERTY_HINT_RESOURCE_TYPE,"MNavigationMeshData"),"set_nav_data","get_nav_data");

    ClassDB::bind_method(D_METHOD("set_start_update","input"), &MNavigationRegion3D::set_start_update);
    ClassDB::bind_method(D_METHOD("get_start_update"), &MNavigationRegion3D::get_start_update);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL,"start_update"),"set_start_update","get_start_update");

    ClassDB::bind_method(D_METHOD("set_active_update_loop","input"), &MNavigationRegion3D::set_active_update_loop);
    ClassDB::bind_method(D_METHOD("get_active_update_loop"), &MNavigationRegion3D::get_active_update_loop);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL,"active_update_loop"),"set_active_update_loop","get_active_update_loop");

    ClassDB::bind_method(D_METHOD("set_distance_update_threshold","input"), &MNavigationRegion3D::set_distance_update_threshold);
    ClassDB::bind_method(D_METHOD("get_distance_update_threshold"), &MNavigationRegion3D::get_distance_update_threshold);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"distance_update_threshold"),"set_distance_update_threshold","get_distance_update_threshold");

    ClassDB::bind_method(D_METHOD("set_navigation_radius","input"), &MNavigationRegion3D::set_navigation_radius);
    ClassDB::bind_method(D_METHOD("get_navigation_radius"), &MNavigationRegion3D::get_navigation_radius);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT,"navigation_radius"),"set_navigation_radius","get_navigation_radius");
    ClassDB::bind_method(D_METHOD("set_max_shown_lod","input"), &MNavigationRegion3D::set_max_shown_lod);
    ClassDB::bind_method(D_METHOD("get_max_shown_lod"), &MNavigationRegion3D::get_max_shown_lod);
    ADD_PROPERTY(PropertyInfo(Variant::INT,"max_shown_lod"), "set_max_shown_lod", "get_max_shown_lod");
    ADD_SIGNAL(MethodInfo("navigation_region_is_ready"));
    ADD_SIGNAL(MethodInfo("update_navmesh"));
}


MNavigationRegion3D::MNavigationRegion3D(){
    update_timer = memnew(Timer);
    add_child(update_timer);
    update_timer->set_one_shot(false);
    update_timer->set_wait_time(0.05);
    update_timer->connect("timeout",Callable(this,"_update_loop"));
    dirty_points_id = memnew(VSet<int>);
    //Debug mesh
    debug_mesh_instance = memnew(MeshInstance3D);
    add_child(debug_mesh_instance);
    debug_mesh.instantiate();
    debug_mesh_instance->set_mesh(debug_mesh);
}

MNavigationRegion3D::~MNavigationRegion3D(){

}

void MNavigationRegion3D::init(MTerrain* _terrain, MGrid* _grid){
    terrain = _terrain;
    grid = _grid;
    if(start_update){
        get_cam_pos();
        update_navmesh(cam_pos);
        last_update_pos = cam_pos;
        update_timer->start();
    }
    ERR_FAIL_COND(!grid->is_created());
    h_scale = grid->get_h_scale();
    scenario = grid->get_scenario();
    region_pixel_width = (uint32_t)round((float)grid->region_size_meter/h_scale);
    region_pixel_size = region_pixel_width*region_pixel_width;
    base_grid_size_in_pixel=(uint32_t)round((double)region_pixel_width/(double)grid->region_size);
    width = region_pixel_width*grid->get_region_grid_size().x;
    height = region_pixel_width*grid->get_region_grid_size().z;
    UtilityFunctions::print("W ",width, " H ",height);
    region_grid_width = grid->get_region_grid_size().x;
    if(nav_data.is_valid()){
        UtilityFunctions::print("Nav is valid");
        UtilityFunctions::print("Nav Size is ", nav_data->data.size());
        int64_t data_size = ((region_pixel_size*grid->get_regions_count() - 1)/8) + 1;
        ERR_FAIL_COND_MSG(nav_data->data.size()!=0&&nav_data->data.size()!=data_size,"Terrain Size or Terrain Base Size has been changed and this data is not valid anymore");
        if(nav_data->data.size()==0){
            nav_data->data.resize(data_size);
            if(nav_data->on_all_at_creation){
                nav_data->data.fill(255);
            }
        }
    }
    UtilityFunctions::print("Region pixel width ",region_pixel_width);
    UtilityFunctions::print("base_grid_size_in_pixel ",base_grid_size_in_pixel);
    //Paint Mesh And material
    PackedVector3Array vert;
    float j = (h_scale - 0.05)*0.9;
    //f1
    vert.push_back(Vector3(0,0.3,0));
    vert.push_back(Vector3(j,0.3,0));
    vert.push_back(Vector3(0,0.3,j));
    //f2
    vert.push_back(Vector3(j,0.3,0));
    vert.push_back(Vector3(j,0.3,j));
    vert.push_back(Vector3(0,0.3,j));
    Array arrm;
    arrm.resize(Mesh::ARRAY_MAX);
    arrm[ArrayMesh::ARRAY_VERTEX] = vert;
    paint_mesh.instantiate();
    paint_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES,arrm);

    paint_material.instantiate();
    paint_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    paint_material->set_albedo(Color(0.1,0.1,0.4));

    is_nav_init = true;
    emit_signal("navigation_region_is_ready");
}

void MNavigationRegion3D::clear(){
    debug_mesh->clear_surfaces();
    std::lock_guard<std::mutex> lock(npoint_mutex);
    for(HashMap<int64_t,MGrassChunk*>::Iterator it = grid_to_npoint.begin();it!=grid_to_npoint.end();++it){
        memdelete(it->value);
    }
    grid_to_npoint.clear();
}

void MNavigationRegion3D::_update_loop(){
    get_cam_pos();
    float dis = (last_update_pos - cam_pos).length();
    if(dis>distance_update_threshold && !is_updating){
        update_navmesh(cam_pos);
    }
}

void MNavigationRegion3D::update_navmesh(Vector3 cam_pos){
    ERR_FAIL_COND(is_updating);
    if(!grid->is_created()){
        return;
    }
    ERR_FAIL_COND(!get_navigation_mesh().is_valid());
    is_updating = true;
    update_thread = std::async(std::launch::async, &MNavigationRegion3D::_update_navmesh, this, cam_pos);
}

void MNavigationRegion3D::_update_navmesh(Vector3 cam_pos){
    UtilityFunctions::print("Update navmesh");
    tmp_nav = get_navigation_mesh()->duplicate();
    uint32_t l = round(tmp_nav->get_detail_sample_distance()/h_scale);
    
    Vector2i top_left = grid->get_closest_pixel(cam_pos - Vector3(navigation_radius,0,navigation_radius));
    Vector2i bottom_right = grid->get_closest_pixel(cam_pos + Vector3(navigation_radius,0,navigation_radius));
    if(bottom_right.x < 0 || bottom_right.y <0 || top_left.x > (int)grid->grid_pixel_region.right || top_left.y > (int)grid->grid_pixel_region.bottom){
        last_update_pos = cam_pos;
        tmp_nav->clear_polygons();
        call_deferred("_finish_update",tmp_nav);
        return;
    }
    uint32_t left = top_left.x < 0 ? 0 : top_left.x;
    uint32_t top = top_left.y < 0 ? 0 : top_left.y;
    uint32_t right = bottom_right.x > grid->grid_pixel_region.right ? grid->grid_pixel_region.right : bottom_right.x;
    uint32_t bottom = bottom_right.y > grid->grid_pixel_region.bottom ? grid->grid_pixel_region.bottom : bottom_right.y;
    UtilityFunctions::print("left ",left," right ",right, " top ",top," bottom ",bottom);
    Ref<NavigationMeshSourceGeometryData3D> geo_data;
    geo_data.instantiate();
    PackedVector3Array faces;
    for(uint32_t y=top;y<bottom;y+=l){
        for(uint32_t x=left;x<right;x+=l){
            bool has_null_npoint = false;
            int end_y = y+l;
            int end_x = x+l;
            for(uint32_t j=y;j<end_y;j++){
                for(uint32_t i=x;i<end_x;i++){
                    if(!get_npoint_by_pixel(i,j)){
                        has_null_npoint = true;
                        break;
                    }
                }
                if(has_null_npoint){
                    break;
                }
            }
            if(!has_null_npoint){
                Vector3 top_left = grid->get_pixel_world_pos(x,y);
                Vector3 top_right = grid->get_pixel_world_pos(x+l,y);
                Vector3 bottom_left = grid->get_pixel_world_pos(x,y+l);
                Vector3 bottom_right = grid->get_pixel_world_pos(x+l,y+l);
                faces.push_back(top_right);
                faces.push_back(bottom_left);
                faces.push_back(top_left);
                faces.push_back(top_right);
                faces.push_back(bottom_right);
                faces.push_back(bottom_left);
            }
        }
    }
    if(faces.size()==0){
        last_update_pos = cam_pos;
        tmp_nav->clear_polygons();
        call_deferred("_finish_update",tmp_nav);
        return;
    }
    Basis b;
    Transform3D t(b,Vector3(0,0,0));
    geo_data->add_faces(faces,t);
    // Add Grass Col
    faces.clear();
    for(int i=0;i<terrain->confirm_grass_col_list.size();i++){
        MGrass* g = terrain->confirm_grass_col_list[i];
        float r=g->get_nav_obstacle_radius();
        float h0=tmp_nav->get_agent_height()*0.5;
        float h=tmp_nav->get_agent_height()*0.5;
        Vector3 otl(-r,h0,-r);
        Vector3 otr(r,h0,-r);
        Vector3 obr(r,h0,r);
        Vector3 obl(-r,h0,r);
        Vector3 ou(0,h,0);
        PackedVector3Array positions = g->get_physic_positions(cam_pos,navigation_radius);
        for(int k=0;k<positions.size();k++){
            Vector3 fpos = positions[k];
            Vector3 tl=fpos+otl;
            Vector3 tr=fpos+otr;
            Vector3 br=fpos+obr;
            Vector3 bl=fpos+obl;
            Vector3 u=fpos+ou;
            //f1
            faces.push_back(tr);
            faces.push_back(tl);
            faces.push_back(u);
            //f2
            faces.push_back(br);
            faces.push_back(tr);
            faces.push_back(u);
            //f3
            faces.push_back(bl);
            faces.push_back(br);
            faces.push_back(u);
            //f4
            faces.push_back(tl);
            faces.push_back(bl);
            faces.push_back(u);
            //f5
            //faces.push_back(tr);
            //faces.push_back(tl);
            //faces.push_back(bl);
            //f6
            //faces.push_back(tr);
            //faces.push_back(bl);
            //faces.push_back(br);
        }
    }
    //Adding
    if(faces.size()>0){
        geo_data->add_faces(faces,t);

        //Debug mesh
        Array debug_arr;
        debug_arr.resize(Mesh::ARRAY_MAX);
        debug_arr[Mesh::ARRAY_VERTEX] = faces;
        debug_mesh->call_deferred("clear_surfaces");
        debug_mesh->call_deferred("add_surface_from_arrays",Mesh::PRIMITIVE_TRIANGLES,debug_arr);
        //debug_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, debug_arr);
        debug_mesh->call_deferred("clear_surfaces");
        debug_mesh->call_deferred("add_surface_from_arrays",Mesh::PRIMITIVE_TRIANGLES,debug_arr);
    }
    UtilityFunctions::print("Finish adding second  faces");

    NavigationServer3D::get_singleton()->bake_from_source_geometry_data(tmp_nav,geo_data);
    last_update_pos = cam_pos;
    UtilityFunctions::print("Finish Thread ");
    obs_info.clear();
    obst_info.clear();
    call_deferred("_finish_update",tmp_nav);
}

void MNavigationRegion3D::_finish_update(Ref<NavigationMesh> nvm){
    emit_signal("update_navmesh");
    UtilityFunctions::print("Finish Updating  ");
    update_thread.wait();
    call_deferred("set_navigation_mesh",nvm);
    call_deferred("update_gizmos");
    call_deferred("_set_is_updating",false);
    UtilityFunctions::print("Finish set navmesh  ");
}

void MNavigationRegion3D::_set_is_updating(bool input){
    is_updating = input;
}


void MNavigationRegion3D::get_cam_pos(){
    if(custom_camera != nullptr){
        cam_pos = custom_camera->get_global_position();
        return;
    }
    if(terrain->editor_camera !=nullptr){
        cam_pos = terrain->editor_camera->get_global_position();
        return;
    }
    Viewport* v = get_viewport();
    Camera3D* camera = v->get_camera_3d();
    ERR_FAIL_COND_EDMSG(camera==nullptr, "No camera is detected, For MNavigationRegion3D");
    cam_pos = camera->get_global_position();
}

void MNavigationRegion3D::force_update(){
    _force_update = true;
}

void MNavigationRegion3D::set_nav_data(Ref<MNavigationMeshData> input){
    nav_data = input;
}

Ref<MNavigationMeshData> MNavigationRegion3D::get_nav_data(){
    return nav_data;
}

void MNavigationRegion3D::set_start_update(bool input){
    start_update = input;
}
bool MNavigationRegion3D::get_start_update(){
    return start_update;
}

void MNavigationRegion3D::set_active_update_loop(bool input){
    if(input){
        update_timer->start();
    } else {
        update_timer->stop();
    }
    active_update_loop = input;
}

bool MNavigationRegion3D::get_active_update_loop(){
    return active_update_loop;
}

void MNavigationRegion3D::set_distance_update_threshold(float input){
    ERR_FAIL_COND(input<2);
    distance_update_threshold = input;
}

float MNavigationRegion3D::get_distance_update_threshold(){
    return distance_update_threshold;
}

void MNavigationRegion3D::set_navigation_radius(float input){
    navigation_radius = input;
}
float MNavigationRegion3D::get_navigation_radius(){
    return navigation_radius;
}

void MNavigationRegion3D::set_max_shown_lod(int input){
    max_shown_lod = input;
}

int MNavigationRegion3D::get_max_shown_lod(){
    return max_shown_lod;
}

void MNavigationRegion3D::update_npoints(){
    int new_chunk_count = grid->grid_update_info.size();
    std::lock_guard<std::mutex> lock(npoint_mutex);
    update_id = grid->get_update_id();
    for(int i=0;i<new_chunk_count;i++){
        if(grid->grid_update_info[i].lod>max_shown_lod){
            continue;
        }
        create_npoints(i);
    }
}
void MNavigationRegion3D::update_dirty_npoints(){
    ERR_FAIL_COND(!is_nav_init);
    std::lock_guard<std::mutex> lock(npoint_mutex);
    for(int i=0;i<dirty_points_id->size();i++){
        //UtilityFunctions::print("dirty_points ",(*dirty_points_id)[i]);
        int64_t terrain_instance_id = grid->get_point_instance_id_by_point_id((*dirty_points_id)[i]);
        //UtilityFunctions::print("terrain_instance_id ",terrain_instance_id);
        if(!grid_to_npoint.has(terrain_instance_id)){
            continue;
        }
        if(!grid_to_npoint.has(terrain_instance_id)){
            continue;
        }
        MGrassChunk* g = grid_to_npoint[terrain_instance_id];
        //UtilityFunctions::print("MGrassChunk count ",g->count, " right ",g->pixel_region.right);
        create_npoints(-1,g);
    }
    memdelete(dirty_points_id);
    dirty_points_id = memnew(VSet<int>);
}
void MNavigationRegion3D::apply_update_npoints(){
    std::lock_guard<std::mutex> lock(npoint_mutex);
    for(int i=0;i<grid->remove_instance_list.size();i++){
        if(grid_to_npoint.has(grid->remove_instance_list[i].get_id())){
            MGrassChunk* g = grid_to_npoint.get(grid->remove_instance_list[i].get_id());
            memdelete(g);
            grid_to_npoint.erase(grid->remove_instance_list[i].get_id());
        }
    }
    to_be_visible.clear();
}

void MNavigationRegion3D::create_npoints(int grid_index,MGrassChunk* grass_chunk){
    //UtilityFunctions::print("Adding npoint start");
    MGrassChunk* g;
    MPixelRegion px;
    if(grass_chunk==nullptr){
        px.left = (uint32_t)round(((double)region_pixel_width)*CHUNK_INFO.region_offset_ratio.x);
        px.top = (uint32_t)round(((double)region_pixel_width)*CHUNK_INFO.region_offset_ratio.y);
        int size_scale = pow(2,CHUNK_INFO.chunk_size);
        //UtilityFunctions::print("Size scale ",size_scale);
        px.right = px.left + base_grid_size_in_pixel*size_scale - 1;
        px.bottom = px.top + base_grid_size_in_pixel*size_scale - 1;
        // We keep the chunk information for grass only in root grass chunk
        g = memnew(MGrassChunk(px,CHUNK_INFO.region_world_pos,CHUNK_INFO.lod,CHUNK_INFO.region_id));
        grid_to_npoint.insert(CHUNK_INFO.terrain_instance.get_id(),g);
    } else {
        g = grass_chunk;
        px = grass_chunk->pixel_region;
    }
    //UtilityFunctions::print("Region pixel size ",region_pixel_width);
    //UtilityFunctions::print("Left ",px.left, " Right ",px.right," bottom ",px.bottom, " top ",px.top);

    const uint8_t* ptr = nav_data->data.ptr() + g->region_id*region_pixel_size/8;
    int lod_scale = pow(2,g->lod);
    uint32_t count=0;
    uint32_t index;
    uint32_t x=px.left;
    uint32_t y=px.top;
    uint32_t i=0;
    uint32_t j=1;
    PackedFloat32Array buffer;
    while (true)
    {
        while (true){
            x = px.left + i*lod_scale;
            if(x>px.right){
                break;
            }
            i++;
            uint32_t offs = (y*region_pixel_width + x);
            uint32_t ibyte = offs/8;
            uint32_t ibit = offs%8;
            
            if( (ptr[ibyte] & (1 << ibit)) != 0){
                index=count*BUFFER_STRID_FLOAT;
                buffer.resize(buffer.size()+12);
                float* ptrw = (float*)buffer.ptrw();
                ptrw += index;
                Vector3 pos;
                ptrw[0]=lod_scale;
                ptrw[1]=0;
                ptrw[2]=0;
                ptrw[4]=0;
                ptrw[5]=lod_scale;
                ptrw[6]=0;
                ptrw[8]=0;
                ptrw[9]=0;
                ptrw[10]=lod_scale;
                pos.x = g->world_pos.x + x*h_scale;
                pos.z = g->world_pos.z + y*h_scale;
                
                ptrw[3] = pos.x;
                ptrw[7] += grid->get_height(pos);
                ptrw[11] = pos.z;
                count++;
            }
        }
        i= 0;
        y= px.top + j*lod_scale;
        if(y>px.bottom){
            break;
        }
        j++;
    }
    //UtilityFunctions::print("Stage 4.1 k ",k);
    // Discard grass chunk in case there is no mesh RID or count is less than min_grass_cutoff
    if(count == 0){
        g->set_buffer(0,RID(),RID(),RID(),PackedFloat32Array());
        return;
    }
    g->set_buffer(count,scenario,paint_mesh->get_rid(),paint_material->get_rid(),buffer);
    g->set_shadow_setting(RenderingServer::SHADOW_CASTING_SETTING_OFF);

    g->unrelax();
    //g->total_count = count;
    //if(is_npoints_visible)
    //    g->unrelax();
   // else
//g->relax();
}
void MNavigationRegion3D::set_npoint_by_pixel(uint32_t px, uint32_t py, bool p_value){
    ERR_FAIL_COND(!is_nav_init);
    ERR_FAIL_INDEX(px, width);
    ERR_FAIL_INDEX(py, height);
    Vector2 flat_pos(float(px)*h_scale,float(py)*h_scale);
    int point_id = grid->get_point_id_by_non_offs_ws(flat_pos);
    dirty_points_id->insert(point_id);
    uint32_t rx = px/region_pixel_width;
    uint32_t ry = py/region_pixel_width;
    uint32_t rid = ry*region_grid_width + rx;
    uint32_t x = px%region_pixel_width;
    uint32_t y = py%region_pixel_width;
    uint32_t offs = rid*region_pixel_size + y*region_pixel_width + x;
    uint32_t ibyte = offs/8;
    uint32_t ibit = offs%8;
    uint8_t b = nav_data->data[ibyte];
    if(p_value){
        b |= (1 << ibit);
    } else {
        b &= ~(1 << ibit);
    }
    nav_data->data.set(ibyte,b);
}

bool MNavigationRegion3D::get_npoint_by_pixel(uint32_t px, uint32_t py){
    ERR_FAIL_COND_V(!is_nav_init,false);
    ERR_FAIL_INDEX_V(px, width,false);
    ERR_FAIL_INDEX_V(py, height,false);
    uint32_t rx = px/region_pixel_width;
    uint32_t ry = py/region_pixel_width;
    uint32_t rid = ry*region_grid_width + rx;
    uint32_t x = px%region_pixel_width;
    uint32_t y = py%region_pixel_width;
    uint32_t offs = rid*region_pixel_size + y*region_pixel_width + x;
    uint32_t ibyte = offs/8;
    uint32_t ibit = offs%8;
    return (nav_data->data[ibyte] & (1 << ibit)) != 0;
}

Vector2i MNavigationRegion3D::get_closest_pixel(Vector3 pos){
    pos -= grid->offset;
    pos = pos / h_scale;
    return Vector2i(round(pos.x),round(pos.z));
}

void MNavigationRegion3D::draw_npoints(Vector3 brush_pos,real_t radius,bool add){
    ERR_FAIL_COND(!is_nav_init);
    ERR_FAIL_COND(update_id!=grid->get_update_id());
    Vector2i px_pos = get_closest_pixel(brush_pos);
    if(px_pos.x<0 || px_pos.y<0 || px_pos.x>width || px_pos.y>height){
        return;
    }
    uint32_t brush_px_radius = (uint32_t)(radius/h_scale);
    uint32_t brush_px_pos_x = px_pos.x;
    uint32_t brush_px_pos_y = px_pos.y;
    // Setting left right top bottom
    MPixelRegion px;
    px.left = (brush_px_pos_x>brush_px_radius) ? brush_px_pos_x - brush_px_radius : 0;
    px.right = brush_px_pos_x + brush_px_radius;
    px.right = px.right > (width-2) ? (width-2) : px.right;
    px.top = (brush_px_pos_y>brush_px_radius) ? brush_px_pos_y - brush_px_radius : 0;
    px.bottom = brush_px_pos_y + brush_px_radius;
    px.bottom = (px.bottom>(height-2)) ? (height-2) : px.bottom;
    //UtilityFunctions::print("brush pos ", brush_pos);
    //UtilityFunctions::print("draw R ",brush_px_radius);
    //UtilityFunctions::print("L ",itos(px.left)," R ",itos(px.right)," T ",itos(px.top), " B ",itos(px.bottom));
    // LOD Scale
    //int lod_scale = pow(2,lod);
    // LOOP
    uint32_t x=px.left;
    uint32_t y=px.top;
    uint32_t i=0;
    uint32_t j=1;
    for(uint32_t y = px.top; y<=px.bottom;y++){
        for(uint32_t x = px.left; x<=px.right;x++){
            uint32_t dif_x = abs(x - brush_px_pos_x);
            uint32_t dif_y = abs(y - brush_px_pos_y);
            uint32_t dis = sqrt(dif_x*dif_x + dif_y*dif_y);
            if(dis<brush_px_radius)
                set_npoint_by_pixel(x,y,add);
        }
    }
    update_dirty_npoints();
}

void MNavigationRegion3D::set_npoints_visible(bool val){
    std::lock_guard<std::mutex> lock(npoint_mutex);
    if(val == is_npoints_visible){
        return;
    }
    is_npoints_visible = val;
    for(HashMap<int64_t,MGrassChunk*>::Iterator it = grid_to_npoint.begin();it!=grid_to_npoint.end();++it){
        if(val){
            it->value->unrelax();
        } else {
            it->value->relax();
        }
    }
}