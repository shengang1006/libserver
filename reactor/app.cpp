#include "app.h"
#include "server.h"

/********app_connection*********/
app_connection::app_connection(int epfd, int fd)
:connection(epfd, fd){
	m_appid = -1;
}

int app_connection::get_appid(){
	return m_appid;
}

void app_connection::set_appid(int appid){
	m_appid = appid;
}

int app_connection::post_send(char * data, int len){
	auto_lock __lock(m_mutex);
	return connection::post_send(data, len);
}

int app_connection::post_send(){
	auto_lock __lock(m_mutex);
	return connection::post_send();
}

int app_connection::get_wait_to_send_data_len(){
	auto_lock __lock(m_mutex);
	return connection::get_wait_to_send_data_len();
}	
/********app_timer*********/	
int app_timer::init(int precision /*= 1000*/){
	m_precision = precision;
	auto_lock __lock(m_mutex);
	return m_timer.init();
}

int app_timer::release(){
	auto_lock __lock(m_mutex);
	return m_timer.release();
}

int app_timer::add(int id, int interval, void* data){
	auto_lock __lock(m_mutex);
	return m_timer.add(id, interval, data);
}

int app_timer::pop_timeout(evtime & ev){
	auto_lock __lock(m_mutex);
	return m_timer.pop_timeout(ev);
}

int app_timer::latency_time(){
	auto_lock __lock(m_mutex);
	int timeout = m_timer.latency_time();
	if(timeout < 0 || timeout > m_precision){
		return m_precision;
	}
	return timeout;
}
	
/********app*********/
app::app(){
	m_brun = false;
	m_drop_msg = 0;
	m_appid = 0;
	memset(m_name, 0, sizeof(m_name));
	m_mutil_thread = true;
	m_svr = NULL;
}

app::~app(){
	m_brun = false;
	m_drop_msg = 0;
}

void * app::app_run(void* param){
	app * s = (app*)param;
	s->run();
	return 0;
}
	
int app::run(){
	
	while(m_brun){
		
		void* data = NULL;
		if(m_ring_buf.pop(data) == -1){
			continue;
		}
		dispatch((app_hd*)data);
		free(data);
	}
	return 0;
}

int app::handle_write(app_connection * n){
	n->post_send();
	if(!n->get_wait_to_send_data_len()){
		return on_write(n);
	}
	else{
		return 0;
	}
}
	
int app::on_write(app_connection * n){
	return 0;
}

int app::dispatch(const app_hd * msg){
	
	if(msg->type == tcp_type){
		switch(msg->event){
			case ev_sys_recv:
				on_recv(msg->u.tcp.n, msg->content, msg->length);
				break;
				
			case ev_sys_close:
				on_close(msg->u.tcp.n, *((int*)msg->content));
				delete msg->u.tcp.n;
				break;
				
			case ev_sys_accept:
				on_accept(msg->u.tcp.n);
				break;
				
			case ev_sys_connect_ok:
				on_connect(msg->u.tcp.n);
				break;
				
			case ev_sys_connect_fail:
				on_connect(msg->u.tcp.n);
				delete msg->u.tcp.n;
			break;
			
			case ev_sys_write:
				handle_write(msg->u.tcp.n);
			break;
		}
	}
	else if(msg->type == timer_type){
		on_timer(msg->event, msg->u.timer.interval, msg->u.timer.ptr);
	}
	else if(msg->type == app_type){
		on_app(msg->event, msg->content, msg->length);
	}
	else{
		error_log("msg from unknown app_name(%s)\n", m_name);
	}
	return 0;	
}

int app::create(server * s, int appid, int msg_count, const char * app_name, bool mutil_thread){
	
	strncpy(m_name, app_name, sizeof(m_name)-1);
	m_appid = appid;
	m_mutil_thread = mutil_thread;
	m_svr = s;
	if(mutil_thread){	
		if(m_ring_buf.create(msg_count) == -1){
			error_log("create ring buffer fail\n");
			return -1;
		}
		m_brun = true;
		if(create_thread(NULL, app_run, m_name, (void*)this) == -1){
			error_log("create_thread fail\n");
			return -1;
		}
	}
	return 0;
}

int app::get_appid(){
	return m_appid;
}

int  app::push(const app_hd & temp){
	
	if(!m_mutil_thread){
		return dispatch(&temp);
	}
	
	app_hd * msg = (app_hd*)malloc(sizeof(app_hd) + temp.length + 1);
	if(!msg){
		error_log("malloc %d bytes fail, error(%d)\n", sizeof(app_hd) + temp.length + 1, errno);
		return -1;
	}
	memcpy(msg, &temp, sizeof(app_hd));
	
	if(temp.length){
		msg->content = (char*)msg + sizeof(app_hd) ;
		memcpy(msg->content, temp.content, temp.length);
		msg->content[temp.length] = 0;	
	}
	
	auto_lock __lock(m_push_mutex);
	int ret = m_ring_buf.push(msg);
	if(ret < 0){
		error_log("drop msg total(%d)\n", ++m_drop_msg);
		free(msg);
	}
	return ret;
}

const char * app::name(){
	return m_name;
}

int app::add_timer(int id, int interval, void * context){
	return m_svr->add_timer(id, interval, m_appid, context);
}


int app::add_abs_timer(int id, int year, int mon, int day, 
					  int hour, int min, int sec, void * context /* = NULL */){

	return m_svr->add_abs_timer(id, year, mon, day, hour, min, sec, m_appid, context);
}

int app::post_connect(const char * ip, ushort port, int delay, void * context){
	return m_svr->post_connect(ip, port, delay, m_appid, context);
}
	
int app::post_app_msg(int dst, int event, void * content, int length){
	return m_svr->post_app_msg(dst, event, content, length);
}