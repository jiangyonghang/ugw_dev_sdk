#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ugw_dev.h"

#define DEVNUM (3)
#define REQNUM (100)

typedef struct uplus_dev
{
    char devid[32+1];
    //其他信息
    int attr1;
    char attr2[32+1];
    int alarm1;//0为无报警,其他数值为有报警
    int change;//属性变化,change=1有变化,change=0无变化
    int up;//-1 已经通知过下线 0 下线 1 上线 2 已经通知过上线
}uplus_dev;

typedef struct request
{
    int devsn;
    int sn;//ugw sdk 请求的sn
    char devid[32+1];
    int type;//请求类型,1 读,2 写,3 操作
    char *name;
    char *value;
    ugw_dev_pair *pairs;
    int pairslen;
}request;

pthread_mutex_t g_dev_mutex;
uplus_dev *g_devs[DEVNUM];
request *g_req[REQNUM];
pthread_mutex_t g_request_mutex;

void push_request(request *req);

//上报线程
void *report_thread(void *arg)
{
    int i=0;
    //循环检查设备状态变化和报警变化
    //如果有变化则调用sdk api通知uGW server
    while(1)
    {
        for(i=0;i<DEVNUM;++i)
        {
            pthread_mutex_lock(&g_dev_mutex);
            //设备上线时调用add_ugw_dev()
            if(g_devs[i]->up==1)
            {
                g_devs[i]->up=2;
                int trt=add_ugw_dev(g_devs[i]->devid,"uplusid","sv","hv");
                if(trt<0)
                {
                    //填加设备出错处理
                }
            }
            if(g_devs[i]->up==2)
            {
                //设备状态改变
                if(g_devs[i]->change==1)
                {
                    ugw_dev_pair *pairs[2];
                    char attr1str[32]={0};
                    sprintf(attr1str,"%d",g_devs[i]->attr1);
                    pairs[0]=malloc_ugw_dev_pair("attr1",attr1str);
                    pairs[1]=malloc_ugw_dev_pair("attr2",g_devs[i]->attr2);

                    //向uGW server上报状态改变
                    int trt=ugw_dev_status_report(g_devs[i]->devid,pairs,2);
                    if(trt==UGW_DEV_OK)
                    {
                    }
                    else
                    {
                        //通信出错处理
                    }
                    free_ugw_dev_pair(pairs[0]);
                    free_ugw_dev_pair(pairs[1]);
                    g_devs[i]->change=0;
                }
                if(g_devs[i]->alarm1!=0)
                {
                   //类似状态上报,调用ugw_dev_alarm_report()
                }
            }
            if(g_devs[i]->up==0)
            {
                g_devs[i]->up=-1;
                //设备下线时调用del_ugw_dev();
                del_ugw_dev(g_devs[i]->devid);
            }
            pthread_mutex_unlock(&g_dev_mutex);
        }
        sleep(1);
    }
}


uplus_dev *find_by_id(const char *id)
{
    //查找
}

int ugw_dev_read_cb(const char *devid,int sn,const char *name)
{
    uplus_dev *dev=find_by_id(devid);
    if(dev)
    {
        request *req=(request*)malloc(sizeof(request));
        req->sn=sn;
        req->type=1;
        req->name=(char *)malloc(strlen(name)+1);
        strcpy(req->name,name);
        push_request(req);
    }
    return 0;
}
int ugw_dev_write_cb(const char *devid,int sn,const char *name,const char *value)
{
    uplus_dev *dev=find_by_id(devid);
    if(dev)
    {
        request *req=(request*)malloc(sizeof(request));
        req->sn=sn;
        req->type=1;
        req->name=(char *)malloc(strlen(name)+1);
        strcpy(req->name,name);
        req->value=(char *)malloc(strlen(value)+1);
        strcpy(req->value,value);
        push_request(req);
    }
    return 0;
}
int ugw_dev_op_cb(const char *devid,int sn,const char *name,ugw_dev_pair* pairs[],int len)
{
    //操作处理
    return 0;
}
int ugw_dev_cloud_state_cb(int state)
{
    //云平台连接状态处理
    return 0;
}


//------------------------请求链表管理区域-----------------------------------------

int g_request_idx=0;
int g_request_len=0;
request* pull_request()
{
    int i=0;
    request *req=0;
    pthread_mutex_lock(&g_request_mutex);
    if(g_request_len>0)
    {
        req=g_req[g_request_idx];
        ++g_request_idx;
        g_request_idx%=REQNUM;
        --g_request_len;
    }
    pthread_mutex_unlock(&g_request_mutex);

    return req;
}

void push_request(request *req)
{
    while(g_request_len>=REQNUM)
    {
        usleep(10000);
    }
    pthread_mutex_lock(&g_request_mutex);
    g_req[(g_request_idx+g_request_len)%REQNUM]=req;
    g_request_len++;
    pthread_mutex_unlock(&g_request_mutex);
}


//---------------------请求链表管理区域-------------------------------------------


int main(int argc,char *argv[])
{
    int i=0;
    for(i=0;i<DEVNUM;++i)
    {
        g_devs[i]=(uplus_dev*)malloc(sizeof(uplus_dev));
        sprintf(g_devs[i]->devid,"ID%d",i);
        g_devs[i]->attr1=10;
        strcpy(g_devs[i]->attr2,"True");
        g_devs[i]->alarm1=0;
        g_devs[i]->change=0;
        g_devs[i]->up=-1;
    }
    ugw_dev_cb cb;
    cb.read_cb=ugw_dev_read_cb;
    cb.write_cb=ugw_dev_write_cb;
    cb.op_cb=ugw_dev_op_cb;
    cb.cloud_cb=ugw_dev_cloud_state_cb;

    pthread_mutex_init(&g_dev_mutex,0);
    pthread_mutex_init(&g_request_mutex,0);
    int trt=init_ugw_dev(cb);
    if(trt!=UGW_DEV_OK)
    {
        //初始化出错处理
    }
    pthread_t tid1,tid2;
    //开启上报线程
    pthread_create(&tid1,0,report_thread,0);

    srand(time(0));
    //创建devnum 个模拟设备
    //模拟智能设备 
    while(1)
    {
        int randnum=rand()%100;
        for(i=0;i<DEVNUM;++i)
        {
            pthread_mutex_lock(&g_dev_mutex);
            if(g_devs[i]->up<=0)
            {
                if(randnum%(i+1)==0)
                {
                    g_devs[i]->up=1;//模拟设备上线
                    g_devs[i]->change=1;//设备上线上报状态
                }
            }
            if(g_devs[i]->up>=1)
            {
                if(randnum>90&&randnum%(i+1)==0)
                {
                    g_devs[i]->up=0;//模拟设备下线
                }
            }
            pthread_mutex_unlock(&g_dev_mutex);
        }

        request *req=pull_request();
        if(req)
        {
            uplus_dev *dev=find_by_id(req->devid);
            if(dev)
            {
                pthread_mutex_lock(&g_dev_mutex);
                if(dev->up<=0)
                {
                    //设备下线回应处理
                }
                else
                {
                    if(req->type==1)
                    {
                        char value[1024]={0};
                        if(strcmp(req->name,"attr1")==0)
                        {
                            sprintf(value,"%d",dev->attr1);
                        }
                        else if(strcmp(req->name,"attr2")==0)
                        {
                            strcpy(value,dev->attr2);
                        }
                        int trt=ugw_dev_read_rsp(req->devid,req->sn,value,0);
                        if(trt<0)
                        {
                            //发送出错处理
                        }
                    }
                    else if(req->type==2)
                    {
                        if(strcmp(req->name,"attr1")==0)
                        {
                            dev->attr1=atoi(req->value);
                        }
                        else if(strcmp(req->name,"attr2")==0)
                        {
                            strcpy(dev->attr2,req->value);
                        }
                        int trt=ugw_dev_write_rsp(req->devid,req->sn,0);
                        dev->change=1;
                    }
                    else if(req->type==3)
                    {
                        //操作处理
                    }
                }
                pthread_mutex_unlock(&g_dev_mutex);
            }
        }
        //随机触发报警
        //随机改变属性
        sleep(1);
    }

    return 0;
}
