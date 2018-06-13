//
//  rocket.cpp
//  OpenTsiolkovsky
//
//  Created by Takahiro Inagawa on 2018/05/31.
//  Copyright © 2018 Takahiro Inagawa. All rights reserved.
//

#include "rocket.hpp"

using namespace std;
namespace odeint = boost::numeric::odeint;

// xxx_g means "global variable"
Vector3d posLLH_init_g;
Vector3d vel_NED_init_g;
Vector3d posECI_init_g;
Vector3d velECI_init_g;
bool flag_separation_g = false;
bool flag_dump_g = false;
bool flag_impact_g = false;
double max_downrange_g = 0.0;
double max_alt_g = 0.0;
Vector2d impact_point_g; // LLH[deg]
Vector3d posECI_dump_init_g;
Vector3d velECI_dump_init_g;

// Constructor from json file.
// @param (input_filename) OpenTsiolkovsky input json file
Rocket::Rocket(string input_filename){
    std::ifstream fin(input_filename);
    if( !fin ){
        std::ifstream fin("./" + input_filename);
        cout << "hoge" << input_filename << endl;
    }
    if( !fin ){
        cout << "Error:Input data file not found" << endl;
        return;
    }
    picojson::value v;
    fin >> v;
    if (std::cin.fail()) {
        std::cerr << picojson::get_last_error() << std::endl;
        return;
    }
    picojson::object& o              = v.get<picojson::object>();
    
    for (int i=1; i<100; i+=1){
        if (! o["stage" + std::to_string(i)].is<picojson::object>()){
            break;
        }
        picojson::object& o_stage = o["stage" + std::to_string(i)].get<picojson::object>();
        RocketStage temp(o_stage, o);
        temp.num_stage = i;
        rs.push_back(temp);
    }
};

// Constructor from json object.
// @param (o_each) each stage object (ex. "stage1" or "stage2"...)
// @param (o) whole rocket object
RocketStage::RocketStage(picojson::object o_each, picojson::object o){
    source_json_object = o;
    picojson::object& o_calc         = o["calculate condition"].get<picojson::object>();
    picojson::object& o_launch       = o["launch"].get<picojson::object>();
    picojson::object& o_wind         = o["wind"].get<picojson::object>();
    
    name = o["name(str)"].get<string>();
    calc_end_time = o_calc["end time[s]"].get<double>();
    calc_step_time = o_calc["time step for output[s]"].get<double>();
    picojson::array& array_pos = o_launch["position LLH[deg,deg,m]"].get<picojson::array>();
    launch_pos_LLH[0] = array_pos[0].get<double>();
    launch_pos_LLH[1] = array_pos[1].get<double>();
    launch_pos_LLH[2] = array_pos[2].get<double>();
    picojson::array& array_vel = o_launch["velocity NED[m/s]"].get<picojson::array>();
    launch_vel_NED[0] = array_vel[0].get<double>();
    launch_vel_NED[1] = array_vel[1].get<double>();
    launch_vel_NED[2] = array_vel[2].get<double>();
    
    wind_file_exist = o_wind["wind file exist?(bool)"].get<bool>();
    wind_file_name = o_wind["wind file name(str)"].get<string>();
    picojson::array& array_wind_const = o_wind["const wind[m/s,deg]"].get<picojson::array>();
    wind_const[0] = array_wind_const[0].get<double>();
    wind_const[1] = array_wind_const[1].get<double>();
    
    if (wind_file_exist) {
        wind_mat = read_csv_vector_3d("./" + wind_file_name,
                                      "altitude[m]", "wind_speed[m/s]", "direction[deg]");
    }

    power_flight_mode = EPower_flight_mode(o_each["power flight mode(int)"].get<double>());
    free_flight_mode = EFree_flight_mode(o_each["free flight mode(int)"].get<double>());
    mass_init = o_each["mass initial[kg]"].get<double>();
    picojson::object& o_thrust   = o_each["thrust"].get<picojson::object>();
    picojson::object& o_aero     = o_each["aero"].get<picojson::object>();
    picojson::object& o_attitude = o_each["attitude"].get<picojson::object>();

    Isp_file_exist = o_thrust["Isp vac file exist?(bool)"].get<bool>();
    Isp_file_name = o_thrust["Isp vac file name(str)"].get<string>();
    Isp_const = o_thrust["const Isp vac[s]"].get<double>();
    thrust_file_exist = o_thrust["thrust vac file exist?(bool)"].get<bool>();
    thrust_file_name = o_thrust["thrust vac file name(str)"].get<string>();
    thrust_const = o_thrust["const thrust vac[N]"].get<double>();
    burn_start_time = o_thrust["burn start time(time of each stage)[s]"].get<double>();
    burn_end_time = o_thrust["burn end time(time of each stage)[s]"].get<double>();
    throat_diameter = o_thrust["throat diameter[m]"].get<double>();
    nozzle_expansion_ratio = o_thrust["nozzle expansion ratio[-]"].get<double>();
    nozzle_exhaust_pressure = o_thrust["nozzle exhaust pressure[Pa]"].get<double>();
    body_diameter = o_aero["body diameter[m]"].get<double>();
    CL_file_exist = o_aero["lift coefficient file exist?(bool)"].get<bool>();
    CL_file_name = o_aero["lift coefficient file name(str)"].get<string>();
    CL_const = o_aero["const lift coefficient[-]"].get<double>();
    CD_file_exist = o_aero["drag coefficient file exist?(bool)"].get<bool>();
    CD_file_name = o_aero["drag coefficient file name(str)"].get<string>();
    CD_const = o_aero["const drag coefficient[-]"].get<double>();
    ballistic_coef = o_aero["ballistic coefficient(ballistic flight mode)[-]"].get<double>();
    attitude_file_exist = o_attitude["attitude file exist?(bool)"].get<bool>();
    attitude_file_name = o_attitude["attitude file name(str)"].get<string>();
    attitude_elevation_const_deg = o_attitude["const elevation[deg]"].get<double>();
    attitude_azimth_const_deg = o_attitude["const azimth[deg]"].get<double>();
    
    try {
        picojson::object& o_dumping  = o_each["dumping product"].get<picojson::object>();
        dump_exist = o_dumping["dumping product exist?(bool)"].get<bool>();
        if (dump_exist){ // if not separation following stage, separation time should be large
            dump_separation_time = o_dumping["dumping product separation time[s]"].get<double>();
        }else{
            dump_separation_time = 1.0e100; // temporaly large number
        }
        dump_mass = o_dumping["dumping product mass[kg]"].get<double>();
        dump_ballistic_coef = o_dumping["dumping product ballistic coefficient[-]"].get<double>();
        picojson::array& array_dump = o_dumping["additional speed at dumping NED[m/s,m/s,m/s]"].get<picojson::array>();
        vel_dump_additional_NEDframe[0] = array_dump[0].get<double>();
        vel_dump_additional_NEDframe[1] = array_dump[1].get<double>();
        vel_dump_additional_NEDframe[2] = array_dump[2].get<double>();
    } catch (...) {
        cout << "dumping product json_object not found" << endl;
    }

    try{
        picojson::object& o_stage    = o_each["stage"].get<picojson::object>();
        following_stage_exist = o_stage["following stage exist?(bool)"].get<bool>();
        if (following_stage_exist){ // if not separation following stage, separation time should be large
            later_stage_separation_time = o_stage["separation time[s]"].get<double>();
        }else{
            later_stage_separation_time = 1.0e100; // temporaly large number
        }
    } catch (...) {
        cout << "stage json_object not found" << endl;
    }
    
    try{
        picojson::object& o_neutrality = o_each["attitude neutrality"].get<picojson::object>();
        is_consider_neutrality = o_neutrality["considering neutrality?(bool)"].get<bool>();
        neutrality_file_name = o_neutrality["CG,CP,Controller position file(str)"].get<string>();
    } catch (...) {
        cout << "attitude neutrality json_object not found" << endl;
    }
    
    body_area = body_diameter * body_diameter * pi / 4.0;
    burn_time = burn_end_time - burn_start_time;
    throat_area = throat_diameter * throat_diameter * pi / 4.0;
    nozzle_exhaust_area = throat_area * nozzle_expansion_ratio;
    
    if ( Isp_file_exist ){
        Isp_mat = read_csv_vector_2d("./" + Isp_file_name, "time[s]", "Isp vac[s]");
    }
    if ( thrust_file_exist ){
        thrust_mat = read_csv_vector_3d("./" + thrust_file_name,
                                        "time[s]", "thrust vac[N]", "nozzle_exhaust_pressure[Pa]");
    }
    if ( CL_file_exist ){
        CL_mat = read_csv_vector_2d("./" + CL_file_name, "mach[-]", "CL[-]");
    }
    if ( CD_file_exist ){
        CD_mat = read_csv_vector_2d("./" + CD_file_name, "mach[-]", "CD[-]");
    }
    if ( attitude_file_exist) {
        attitude_mat = read_csv_vector_3d("./" + attitude_file_name,
                                          "time[s]", "azimth[deg]", "elevation[deg]");
    }
    if ( is_consider_neutrality ) {
        neutrality_mat = read_csv_vector_4d("./" + neutrality_file_name,
                                            "time[s]", "CG_pos_STA[m]", "CP_pos_STA[m]", "Controller_pos_STA[m]");
    }
}

// Constructor for making an instance of ballistic flight object that is a dumping product
// Create a new RocketStage instance from the original RocketStage and
// the position and speed at the time of dumping.
RocketStage::RocketStage(const RocketStage& rocket_stage, Vector3d posECI_init_args, Vector3d velECI_init_args){
    deep_copy(rocket_stage);
    calc_start_time = dump_separation_time;
    free_flight_mode = ballistic_flight;
    mass_init = dump_mass;
    thrust_file_exist = false;
    thrust_const = 0.0;
    burn_start_time = 0.0;
    burn_end_time = 0.0;
    ballistic_coef = dump_ballistic_coef;
    posECI_init = posECI_init_args;
    velECI_init = velECI_init_args;
    
    // for calcurate speed up
    thrust_file_exist = false;
    Isp_file_exist = false;
    CD_file_exist = false;
    CL_file_exist = false;
    attitude_file_exist = false;
}

void Rocket::flight_simulation(){
    // rs : mean RocketNew::rocket_stages, class RocketStage
    using base_stepper_type = odeint::runge_kutta_dopri5<RocketStage::state>;
    auto Stepper = make_dense_output( 1.0e-9 , 1.0e-9 , base_stepper_type());
    
    for (int i = 0; i < rs.size(); i++){  // i is number of the rocket stages
        flag_separation_g = false;
        flag_dump_g = false;
        flag_impact_g = false;
        if (i == 0){
            rs[i].posECI_init << posECI_init(rs[0].launch_pos_LLH);
            rs[i].velECI_init << velECI_init(rs[0].launch_vel_NED, rs[0].launch_pos_LLH);
            rs[i].previous_stage_separation_time = 0.0; // first stage calcuration start time[s]
        } else {
            rs[i].posECI_init << posECI_init_g;
            rs[i].velECI_init << velECI_init_g;
            rs[i].previous_stage_separation_time = rs[i-1].later_stage_separation_time;
        }
        rs[i].calc_start_time = rs[i].previous_stage_separation_time;
        std::vector<double> time_array;
        time_array = {rs[i].calc_start_time,
                      rs[i].dump_separation_time,
                      rs[i].previous_stage_separation_time + rs[i].burn_start_time + rs[i].burn_end_time,
                      rs[i].later_stage_separation_time,
                      rs[i].calc_end_time
        };
        sort(time_array.begin(), time_array.end());  // ascending order for ODE
        std::replace_if(time_array.begin(), time_array.end(),
                        [&](double x){return x > rs[i].calc_end_time;},
                        rs[i].calc_end_time); // keep times to the upper limit(calc_end_time)
        double time_step = rs[i].calc_step_time;
        
        RocketStage::state State = { rs[i].mass_init,
            rs[i].posECI_init[0], rs[i].posECI_init[1], rs[i].posECI_init[2],
            rs[i].velECI_init[0], rs[i].velECI_init[1], rs[i].velECI_init[2],
        };
        string csv_filename = "./output/" + rs[i].name + "_dynamics_" + to_string(rs[i].num_stage) + ".csv";
        CsvObserver Observer(csv_filename, false);
        Observer.deep_copy(rs[i]);
        for (int j = 0; j < time_array.size() - 1; j++){  // j is number of time_array
            odeint::integrate_const(Stepper, rs[i], State,
                                    time_array[j], time_array[j+1], time_step,
                                    std::ref(Observer));
            CsvObserver Observer(csv_filename, true);
            Observer.deep_copy(rs[i]);
            if (time_array[j+1] == rs[i].dump_separation_time){
                RocketStage temp_fo(rs[i], posECI_dump_init_g, velECI_dump_init_g);
                temp_fo.num_stage = rs[i].num_stage;
                fo.push_back(temp_fo);
                State[0] = State[0] - rs[i].dump_mass;  // reduce dumping mass to "ODE x[0]"
            }
        }
        cout << "                                           \r" << flush;
        cout << fixed << setprecision(6) << to_string(rs[i].num_stage) + " stage impact point [deg]:\t";
        cout << impact_point_g[0] << "\t"<< impact_point_g[1] << endl;
        impact_point_g << 0.0,0.0;
        if (!rs[i].following_stage_exist){
            break;
        }
    }
    
    // ==== DUMPING PRODUCTS flight simulation ====
    for (int i = 0; i < fo.size(); i++) {  // i is number of the dumping products
        flag_impact_g = false;
        RocketStage::state State = { fo[i].mass_init,
            fo[i].posECI_init[0], fo[i].posECI_init[1], fo[i].posECI_init[2],
            fo[i].velECI_init[0], fo[i].velECI_init[1], fo[i].velECI_init[2],
        };
        string csv_filename = "./output/" + fo[i].name + "_dynamics_" + to_string(fo[i].num_stage) + "_dump" +  ".csv";
        CsvObserver Observer(csv_filename, false);
        Observer.deep_copy(fo[i]);
        odeint::integrate_const(Stepper, fo[i], State,
                                fo[i].calc_start_time, fo[i].calc_end_time, fo[i].calc_step_time,
                                std::ref(Observer));
        cout << "                                           \r" << flush;
        cout << fixed << setprecision(6) << to_string(fo[i].num_stage) + " stage dumping product impact point [deg]:\t";
        cout << impact_point_g[0] << "\t"<< impact_point_g[1] << endl;
        impact_point_g << 0.0,0.0;
    }

    cout << fixed << setprecision(0);
    cout << "max altitude[m]:\t" << max_alt_g << endl;
    cout << "max downrange[m]:\t" << max_downrange_g << endl;
    cout << "Simulation Success!" << endl;
}

// for odeint::integrate
// x = [mass, x_ECI, y_ECI, z_ECI, vx_ECI, vy_ECI, vz_ECI]
void RocketStage::operator()(const RocketStage::state& x, RocketStage::state& dx, double t){
    posECI_ << x[1], x[2], x[3];
    velECI_ << x[4], x[5], x[6];

    dcmECI2ECEF_ = dcmECI2ECEF(t);
    posECEF_ = posECEF(dcmECI2ECEF_, posECI_);
    posLLH_ = posLLH(posECEF_);

    //    It does not calculate after the rocket falls to the ground
    if ( posLLH_[2] < 0) {
        dx = {0, 0, 0, 0, 0, 0, 0};
        if ((int)t % 10 == 0 && (int)(t*10) % 10 == 0 ){ // progress
            progress(t);
        }
        if ( flag_impact_g == false ) {
            impact_point_g << posLLH_[0], posLLH_[1];
            flag_impact_g = true;
        }
        return;
    }
    
    //    update rocket variables
    update_from_time_and_altitude(t, posLLH_[2]);
    
    //    transform coordinate
    dcmECEF2NED_ = dcmECEF2NED(posLLH_);
    dcmNED2ECEF_ = dcmECEF2NED_.transpose();
    dcmECI2NED_ = dcmECI2NED(dcmECEF2NED_, dcmECI2ECEF_);
    dcmNED2ECI_ = dcmECI2NED_.transpose();
    dcmECEF2NED_init_ = dcmECEF2NED(launch_pos_LLH);
    dcmECI2NED_init_ = dcmECI2NED(dcmECEF2NED_init_, dcmECI2ECEF(0.0));
    vel_ECEF_NEDframe_ = vel_ECEF_NEDframe(dcmECI2NED_, velECI_, posECI_);
    vel_wind_NEDframe_ = vel_wind_NEDframe(wind_speed, wind_direction);
    air = air.altitude(posLLH_[2]);
    //    gravity term : gravity acceleration in the north direction is not considered
    gravity_vector << 0.0, 0.0, - gravity(posLLH_[2], posLLH_[0]);

    Vector2d azel;
    if (is_powered){  // ---- powered flight ----
        switch (power_flight_mode) {
            case _3DoF:
                flight_mode = "power_3DoF";
                dcmNED2BODY_ = dcmNED2BODY(azimth, elevation);
                vel_AIR_BODYframe_ = vel_AIR_BODYframe(dcmNED2BODY_, vel_ECEF_NEDframe_, vel_wind_NEDframe_);
                attack_of_angle_ = attack_of_angle(vel_AIR_BODYframe_);
                dcmBODY2AIR_ = dcmBODY2AIR(attack_of_angle_);
                dcmECI2BODY_ = dcmECI2BODY(dcmNED2BODY_, dcmECI2NED_);
                dcmBODY2ECI_ = dcmECI2BODY_.transpose();

                //    aerodynamics term
                vel_AIR_BODYframe_abs = vel_AIR_BODYframe_.norm();
                mach_number = vel_AIR_BODYframe_abs / air.airspeed;
                update_from_mach_number();
                dynamic_pressure = 0.5 * air.density * vel_AIR_BODYframe_abs * vel_AIR_BODYframe_abs;
                force_drag = CD * dynamic_pressure * body_area;
                force_lift = CL * dynamic_pressure * body_area;
                force_air_vector << -force_drag, 0.0, -force_lift;
                //    thrust term
                if ( is_consider_neutrality ) {
                    force_air_vector_BODYframe = dcmBODY2AIR_.transpose() * force_air_vector;
                    gimbal_angle_pitch = asin(force_air_vector_BODYframe[1] / thrust
                                              * (pos_CP - pos_CG) / (pos_Controller - pos_CG));
                    gimbal_angle_yaw = asin(force_air_vector_BODYframe[2] / thrust
                                            * (pos_CP - pos_CG) / (pos_Controller - pos_CG));
                    force_thrust_vector << thrust * cos(gimbal_angle_yaw) * cos(gimbal_angle_pitch),
                                           thrust * (-1) * sin(gimbal_angle_yaw),
                                           thrust * (-1) * cos(gimbal_angle_yaw) * sin(gimbal_angle_pitch);
                } else {
                    force_thrust_vector << thrust, 0.0, 0.0;  // body coordinate
                }
                //    dv/dt
                accECI_ = 1/x[0] * (dcmBODY2ECI_ * (force_thrust_vector + dcmBODY2AIR_.transpose() * force_air_vector))
                + dcmNED2ECI_ * gravity_vector;
                break;
                
            case _3DoF_with_delay:
                flight_mode = "power_3DoF_delay";
                break;  // Unimplemented
                
            case _6DoF:
                flight_mode = "power_6DoF";
                break;  // Unimplemented
                
            case _6DoF_aerodynami_stable:
                flight_mode = "power_6DoF_aero_stable";
                break;  // Unimplemented
                
            default:
                m_dot = 0;
                accECI_ << 0.0, 0.0, 0.0;
                break;
        }
    } else {  // ---- free flight ----
        switch (free_flight_mode) {
            case aerodynamic_stable:
                flight_mode = "free_aero_stable";
                attack_of_angle_ << 0.0, 0.0, 0.0;
                vel_BODY_NEDframe_ = vel_ECEF_NEDframe_ - vel_wind_NEDframe_;
                vel_AIR_BODYframe_ << vel_BODY_NEDframe_.norm(), 0.0, 0.0;
                azel = azimth_elevaztion(vel_BODY_NEDframe_);
                azimth = azel[0];
                elevation = azel[1];
                dcmNED2BODY_ = dcmNED2BODY(azimth, elevation);
                dcmBODY2AIR_ = dcmBODY2AIR(attack_of_angle_);
                dcmECI2BODY_ = dcmECI2BODY(dcmNED2BODY_, dcmECI2NED_);
                dcmBODY2ECI_ = dcmECI2BODY_.transpose();
                
                //    thrust term
                force_thrust_vector << 0.0, 0.0, 0.0;
                //    aerodynamics term
                vel_AIR_BODYframe_abs = vel_AIR_BODYframe_.norm();
                mach_number = vel_AIR_BODYframe_abs / air.airspeed;
                update_from_mach_number();
                dynamic_pressure = 0.5 * air.density * vel_AIR_BODYframe_abs * vel_AIR_BODYframe_abs;
                force_drag = CD * dynamic_pressure * body_area;
                force_lift = CL * dynamic_pressure * body_area;
                force_air_vector << -force_drag, 0.0, -force_lift;
                //    dv/dt
                accECI_ = 1/x[0] * (dcmBODY2ECI_ * (force_thrust_vector + dcmBODY2AIR_.transpose() * force_air_vector))
                + dcmNED2ECI_ * gravity_vector;
                break;
                
            case _3DoF_defined:
                flight_mode = "free_3dof";
                dcmNED2BODY_ = dcmNED2BODY(azimth, elevation);
                vel_AIR_BODYframe_ = vel_AIR_BODYframe(dcmNED2BODY_, vel_ECEF_NEDframe_, vel_wind_NEDframe_);
                attack_of_angle_ = attack_of_angle(vel_AIR_BODYframe_);
                dcmBODY2AIR_ = dcmBODY2AIR(attack_of_angle_);
                dcmECI2BODY_ = dcmECI2BODY(dcmNED2BODY_, dcmECI2NED_);
                dcmBODY2ECI_ = dcmECI2BODY_.transpose();
                
                //    thrust term
                force_thrust_vector << 0.0, 0.0, 0.0;
                //    aerodynamics term
                vel_AIR_BODYframe_abs = vel_AIR_BODYframe_.norm();
                mach_number = vel_AIR_BODYframe_abs / air.airspeed;
                update_from_mach_number();
                dynamic_pressure = 0.5 * air.density * vel_AIR_BODYframe_abs * vel_AIR_BODYframe_abs;
                force_drag = CD * dynamic_pressure * body_area;
                force_lift = CL * dynamic_pressure * body_area;
                force_air_vector << -force_drag, 0.0, -force_lift;
                //    dv/dt
                accECI_ = 1/x[0] * (dcmBODY2ECI_ * (force_thrust_vector + dcmBODY2AIR_.transpose() * force_air_vector))
                + dcmNED2ECI_ * gravity_vector;
                break;

            case ballistic_flight:
                flight_mode = "free_ballistic";
                //    aerodynamics term
                vel_AIR_NEDframe_ = vel_ECEF_NEDframe_ - vel_wind_NEDframe_;
                vel_AIR_NEDframe_abs = vel_AIR_NEDframe_.norm();
                mach_number = vel_AIR_NEDframe_abs / air.airspeed;
                dynamic_pressure = 0.5 * air.density * vel_AIR_NEDframe_abs * vel_AIR_NEDframe_abs;
                force_drag = dynamic_pressure / ballistic_coef;
                force_air_vector = force_drag * (-1) * (vel_AIR_NEDframe_ / vel_AIR_NEDframe_abs);  //NED frame
                //    dv/dt
                accECI_ = dcmNED2ECI_ * (force_air_vector + gravity_vector);
                break;
                
            default:
                m_dot = 0;
                accECI_ << 0.0, 0.0, 0.0;
                break;
        }
    }

    dx[0] = -m_dot;
    dx[1] = x[4];
    dx[2] = x[5];
    dx[3] = x[6];
    dx[4] = accECI_[0];
    dx[5] = accECI_[1];
    dx[6] = accECI_[2];

    //    separation
    if (flag_separation_g == false && t >= later_stage_separation_time){
        flag_separation_g = true;
        posECI_init_g = posECI_;
        velECI_init_g = velECI_;
    }
    
    // dumping product
    if (flag_dump_g == false && t >= dump_separation_time){
        flag_dump_g = true;
        posECI_dump_init_g = posECI_;
        velECI_dump_init_g = velECI_ + dcmNED2ECI_ * vel_dump_additional_NEDframe;
    }
    
    if (max_alt_g < posLLH_[2]){  // update maximum altitude[m]
        max_alt_g = posLLH_[2];
    }
    downrange = distance_surface(launch_pos_LLH, posLLH_);
    if (max_downrange_g < downrange){  // update maximum downrange
        max_downrange_g = downrange;
    }
    
    if ((int)t % 10 == 0 && (int)(t*10) % 10 == 0 ){ // progress
        progress(t);
    }
}

void RocketStage::update_from_time_and_altitude(double time, double altitude){
    Air air;
    air = air.altitude(altitude);

    if (Isp_file_exist){
        Isp_vac = interp_matrix(time - previous_stage_separation_time, Isp_mat);  // Isp vac
    } else {
        Isp_vac = Isp_const;  // Isp vac
    }
    if (thrust_file_exist){
        thrust_vac = interp_matrix(time - previous_stage_separation_time, thrust_mat, 1);
        nozzle_exhaust_pressure = interp_matrix(time - previous_stage_separation_time, thrust_mat, 2);
        if (thrust_vac != 0) {
            is_powered = true;
        } else {
            is_powered = false;
        }
    } else {
        if(time >= previous_stage_separation_time + burn_start_time &&
           time < previous_stage_separation_time + burn_start_time + burn_time){
            thrust_vac = thrust_const;
            is_powered = true;
        } else {
            is_powered = false;
        }
    }
    if (is_powered){ // ---- powered flight ----
//        thrust_vac = thrust_momentum + nozzle_exhaust_area * nozzle_exhaust_pressure;
        m_dot = thrust_vac / Isp_vac / g0;
        thrust_momentum = thrust_vac - nozzle_exhaust_area * nozzle_exhaust_pressure;
        thrust = thrust_vac - nozzle_exhaust_area * air.pressure;
        if (m_dot > 0.0001) {
            Isp = thrust / m_dot / g0;
        } else {
            Isp = 0.0;
        }
    } else {
        thrust = 0.0;
        m_dot = 0.0;
        Isp = 0.0;
    }
    
    if (attitude_file_exist) {
        azimth = deg2rad(interp_matrix(time, attitude_mat, 1));
        elevation = deg2rad(interp_matrix(time, attitude_mat, 2));
    } else {
        azimth = deg2rad(attitude_azimth_const_deg);
        elevation = deg2rad(attitude_elevation_const_deg);
    }
    
    if (wind_file_exist) {
        wind_speed = interp_matrix(altitude, wind_mat, 1);
        wind_direction = interp_matrix(altitude, wind_mat, 2);
    } else {
        wind_speed = wind_const[0];
        wind_direction = wind_const[1];
    }
    
    if (is_consider_neutrality) {
        pos_CG = interp_matrix(time, neutrality_mat, 1);
        pos_CP = interp_matrix(time, neutrality_mat, 2);
        pos_Controller = interp_matrix(time, neutrality_mat, 3);
    }
    
    if (time >= later_stage_separation_time && is_separated == false){
        is_separated = true;
    }

}

void RocketStage::update_from_mach_number(){
    if (CD_file_exist) {
        CD = interp_matrix(mach_number, CD_mat);
    } else {
        CD = CD_const;
    }
    if (CL_file_exist) {
        CL = interp_matrix(mach_number, CL_mat);
    } else {
        CL = CL_const;
    }
}

// display progress on the console.
void RocketStage::progress(double time_now){
    double time_total = calc_end_time;
    cout << fixed << setprecision(0);
    cout << time_now << "sec / " << time_total << "sec\t@Stage " << num_stage << "\r" << flush;
    return;
}

// for odeint::integrate observer
// x = [mass, x_ECI, y_ECI, z_ECI, vx_ECI, vy_ECI, vz_ECI]
void CsvObserver::operator()(const state& x, double t){
    posECI_ << x[1], x[2], x[3];
    velECI_ << x[4], x[5], x[6];
    
    dcmECI2ECEF_ = dcmECI2ECEF(t);
    posECEF_ = posECEF(dcmECI2ECEF_, posECI_);
    posLLH_ = posLLH(posECEF_);
    
    //    It does not calculate after the rocket falls to the ground
    if ( posLLH_[2] < 0) {
        if ((int)t % 10 == 0 && (int)(t*10) % 10 == 0 ){ // progress
            progress(t);
        }
        return;
    }
    
    //    update rocket variables
    update_from_time_and_altitude(t, posLLH_[2]);
    
    //    transform coordinate
    dcmECEF2NED_ = dcmECEF2NED(posLLH_);
    dcmNED2ECEF_ = dcmECEF2NED_.transpose();
    dcmECI2NED_ = dcmECI2NED(dcmECEF2NED_, dcmECI2ECEF_);
    dcmNED2ECI_ = dcmECI2NED_.transpose();
    dcmECEF2NED_init_ = dcmECEF2NED(launch_pos_LLH);
    dcmECI2NED_init_ = dcmECI2NED(dcmECEF2NED_init_, dcmECI2ECEF(0.0));
    vel_ECEF_NEDframe_ = vel_ECEF_NEDframe(dcmECI2NED_, velECI_, posECI_);
    vel_wind_NEDframe_ = vel_wind_NEDframe(wind_speed, wind_direction);
    air = air.altitude(posLLH_[2]);
    //    gravity term : gravity acceleration in the north direction is not considered
    gravity_vector << 0.0, 0.0, - gravity(posLLH_[2], posLLH_[0]);
    
    Vector2d azel;
    if (is_powered){  // ---- powered flight ----
        switch (power_flight_mode) {
            case _3DoF:
                flight_mode = "power_3DoF";
                dcmNED2BODY_ = dcmNED2BODY(azimth, elevation);
                vel_AIR_BODYframe_ = vel_AIR_BODYframe(dcmNED2BODY_, vel_ECEF_NEDframe_, vel_wind_NEDframe_);
                attack_of_angle_ = attack_of_angle(vel_AIR_BODYframe_);
                dcmBODY2AIR_ = dcmBODY2AIR(attack_of_angle_);
                dcmECI2BODY_ = dcmECI2BODY(dcmNED2BODY_, dcmECI2NED_);
                dcmBODY2ECI_ = dcmECI2BODY_.transpose();
                
                //    aerodynamics term
                vel_AIR_BODYframe_abs = vel_AIR_BODYframe_.norm();
                mach_number = vel_AIR_BODYframe_abs / air.airspeed;
                update_from_mach_number();
                dynamic_pressure = 0.5 * air.density * vel_AIR_BODYframe_abs * vel_AIR_BODYframe_abs;
                force_drag = CD * dynamic_pressure * body_area;
                force_lift = CL * dynamic_pressure * body_area;
                force_air_vector << -force_drag, 0.0, -force_lift;
                //    thrust term
                if ( is_consider_neutrality ) {
                    force_air_vector_BODYframe = dcmBODY2AIR_.transpose() * force_air_vector;
                    gimbal_angle_pitch = asin(force_air_vector_BODYframe[1] / thrust
                                              * (pos_CP - pos_CG) / (pos_Controller - pos_CG));
                    gimbal_angle_yaw = asin(force_air_vector_BODYframe[2] / thrust
                                            * (pos_CP - pos_CG) / (pos_Controller - pos_CG));
                    force_thrust_vector << thrust * cos(gimbal_angle_yaw) * cos(gimbal_angle_pitch),
                    thrust * (-1) * sin(gimbal_angle_yaw),
                    thrust * (-1) * cos(gimbal_angle_yaw) * sin(gimbal_angle_pitch);
                } else {
                    force_thrust_vector << thrust, 0.0, 0.0;  // body coordinate
                }
                //    dv/dt
                accECI_ = 1/x[0] * (dcmBODY2ECI_ * (force_thrust_vector + dcmBODY2AIR_.transpose() * force_air_vector))
                + dcmNED2ECI_ * gravity_vector;
                break;
                
            case _3DoF_with_delay:
                flight_mode = "power_3DoF_delay";
                break;  // Unimplemented
                
            case _6DoF:
                flight_mode = "power_6DoF";
                break;  // Unimplemented
                
            case _6DoF_aerodynami_stable:
                flight_mode = "power_6DoF_aero_stable";
                break;  // Unimplemented
                
            default:
                m_dot = 0;
                accECI_ << 0.0, 0.0, 0.0;
                break;
        }
    } else {  // ---- free flight ----
        switch (free_flight_mode) {
            case aerodynamic_stable:
                flight_mode = "free_aero_stable";
                attack_of_angle_ << 0.0, 0.0, 0.0;
                vel_BODY_NEDframe_ = vel_ECEF_NEDframe_ - vel_wind_NEDframe_;
                vel_AIR_BODYframe_ << vel_BODY_NEDframe_.norm(), 0.0, 0.0;
                azel = azimth_elevaztion(vel_BODY_NEDframe_);
                azimth = azel[0];
                elevation = azel[1];
                dcmNED2BODY_ = dcmNED2BODY(azimth, elevation);
                dcmBODY2AIR_ = dcmBODY2AIR(attack_of_angle_);
                dcmECI2BODY_ = dcmECI2BODY(dcmNED2BODY_, dcmECI2NED_);
                dcmBODY2ECI_ = dcmECI2BODY_.transpose();
                
                //    thrust term
                force_thrust_vector << 0.0, 0.0, 0.0;
                //    aerodynamics term
                vel_AIR_BODYframe_abs = vel_AIR_BODYframe_.norm();
                mach_number = vel_AIR_BODYframe_abs / air.airspeed;
                update_from_mach_number();
                dynamic_pressure = 0.5 * air.density * vel_AIR_BODYframe_abs * vel_AIR_BODYframe_abs;
                force_drag = CD * dynamic_pressure * body_area;
                force_lift = CL * dynamic_pressure * body_area;
                force_air_vector << -force_drag, 0.0, -force_lift;
                //    dv/dt
                accECI_ = 1/x[0] * (dcmBODY2ECI_ * (force_thrust_vector + dcmBODY2AIR_.transpose() * force_air_vector))
                + dcmNED2ECI_ * gravity_vector;
                break;
                
            case _3DoF_defined:
                flight_mode = "free_3dof";
                dcmNED2BODY_ = dcmNED2BODY(azimth, elevation);
                vel_AIR_BODYframe_ = vel_AIR_BODYframe(dcmNED2BODY_, vel_ECEF_NEDframe_, vel_wind_NEDframe_);
                attack_of_angle_ = attack_of_angle(vel_AIR_BODYframe_);
                dcmBODY2AIR_ = dcmBODY2AIR(attack_of_angle_);
                dcmECI2BODY_ = dcmECI2BODY(dcmNED2BODY_, dcmECI2NED_);
                dcmBODY2ECI_ = dcmECI2BODY_.transpose();
                
                //    thrust term
                force_thrust_vector << 0.0, 0.0, 0.0;
                //    aerodynamics term
                vel_AIR_BODYframe_abs = vel_AIR_BODYframe_.norm();
                mach_number = vel_AIR_BODYframe_abs / air.airspeed;
                update_from_mach_number();
                dynamic_pressure = 0.5 * air.density * vel_AIR_BODYframe_abs * vel_AIR_BODYframe_abs;
                force_drag = CD * dynamic_pressure * body_area;
                force_lift = CL * dynamic_pressure * body_area;
                force_air_vector << -force_drag, 0.0, -force_lift;
                //    dv/dt
                accECI_ = 1/x[0] * (dcmBODY2ECI_ * (force_thrust_vector + dcmBODY2AIR_.transpose() * force_air_vector))
                + dcmNED2ECI_ * gravity_vector;
                break;
                
            case ballistic_flight:
                flight_mode = "free_ballistic";
                //    aerodynamics term
                vel_AIR_NEDframe_ = vel_ECEF_NEDframe_ - vel_wind_NEDframe_;
                vel_AIR_NEDframe_abs = vel_AIR_NEDframe_.norm();
                mach_number = vel_AIR_NEDframe_abs / air.airspeed;
                dynamic_pressure = 0.5 * air.density * vel_AIR_NEDframe_abs * vel_AIR_NEDframe_abs;
                force_drag = dynamic_pressure / ballistic_coef;
                force_air_vector = force_drag * (-1) * (vel_AIR_NEDframe_ / vel_AIR_NEDframe_abs);  //NED frame
                //    dv/dt
                accECI_ = dcmNED2ECI_ * (force_air_vector + gravity_vector);
                break;
                
            default:
                m_dot = 0;
                accECI_ << 0.0, 0.0, 0.0;
                break;
        }
    }
    
    // === following code is different from RocketStage::operator() ===
    accBODY_ = dcmECI2BODY_ * (accECI_ - dcmNED2ECI_ * gravity_vector);
    downrange = distance_surface(launch_pos_LLH, posLLH_);
    posLLH_IIP_ = posLLH_IIP(t, posECI_, vel_ECEF_NEDframe_);
    kinematic_energy = 0.5 * x[0] * vel_ECEF_NEDframe_.norm() * vel_ECEF_NEDframe_.norm();
    
    //   ==== Calculte loss velocisy ====
    if (thrust > 0.1 || is_separated == false){
        double vel_ECEF_NEDframe_XY = sqrt(vel_ECEF_NEDframe_[0] * vel_ECEF_NEDframe_[0] +
                                           vel_ECEF_NEDframe_[1] * vel_ECEF_NEDframe_[1]);
        double path_angle_rad = atan2(-vel_ECEF_NEDframe_[2], vel_ECEF_NEDframe_XY);
        loss_gravity = gravity_vector[2] * sin(path_angle_rad);
    } else {
        loss_gravity = 0;
    }
    if (thrust > 0.1){
        loss_thrust = air.pressure * nozzle_exhaust_area / x[0];
    } else {
        loss_thrust = 0;
    }
    loss_aerodynamics = force_drag / x[0];
    loss_total = loss_gravity + loss_aerodynamics + loss_thrust;
        
    // ==== OUTPUT to CSV ====
    // if rocket was already impacted on the earth, NOT OUTPUT.
    if ( posLLH_[2] > 0) {
        fout << t << "," << x[0] << "," << thrust << ",";
        fout.precision(8);
        fout << posLLH_[0] << "," << posLLH_[1] << "," << posLLH_[2] << ","
        << x[1] << "," << x[2] << "," << x[3] << ","
        << x[4] << "," << x[5] << "," << x[6] << ","
        << vel_ECEF_NEDframe_[0] << "," << vel_ECEF_NEDframe_[1] << ","
        << vel_ECEF_NEDframe_[2] << ","
        << accECI_[0] << "," << accECI_[1] << "," << accECI_[2] << ","
        << accBODY_[0] << "," << accBODY_[1] << "," << accBODY_[2] << ","
        << Isp << "," << mach_number << ","
        << rad2deg(azimth) << "," << rad2deg(elevation) << ","
        << rad2deg(attack_of_angle_[0]) << "," << rad2deg(attack_of_angle_[1]) << ","
        << rad2deg(attack_of_angle_[2]) << ","
        << dynamic_pressure << "," << force_drag << "," << force_lift << ","
        << force_air_vector_BODYframe[0] << "," << force_air_vector_BODYframe[1] << ","
        << force_air_vector_BODYframe[2] << "," << force_thrust_vector[0] << ","
        << force_thrust_vector[1] << "," << force_thrust_vector[2] << ","
        << rad2deg(gimbal_angle_pitch) << "," << rad2deg(gimbal_angle_yaw) << ","
        << wind_speed << "," << wind_direction << ","
        << downrange << "," << posLLH_IIP_[0] << "," << posLLH_IIP_[1] << ","
        << dcmBODY2ECI_(0, 0) << "," << dcmBODY2ECI_(0, 1) << "," << dcmBODY2ECI_(0, 2) << ","
        << dcmBODY2ECI_(1, 0) << "," << dcmBODY2ECI_(1, 1) << "," << dcmBODY2ECI_(1, 2) << ","
        << dcmBODY2ECI_(2, 0) << "," << dcmBODY2ECI_(2, 1) << "," << dcmBODY2ECI_(2, 2) << ","
        << int(velECI_.norm()) << "," << kinematic_energy << ","
        << loss_gravity << "," << loss_aerodynamics << "," << loss_thrust << ","
        << is_powered << "," << is_separated << ","
        << endl;
    }
}

