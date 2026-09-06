#include "mqttclient.h"
#include "transport.h"
#include "MQTTPacket.h"
#include "FreeRTOS.h"
#include "task.h"
#include "string.h"
#include "sockets.h"

#include "lwip/opt.h"

#include "lwip/sys.h"
#include "lwip/api.h"

#include "lwip/sockets.h"

#include "cJSON_Process.h"
#include "drv_dht11.h"

/******************************* ȫ�ֱ������� ************************************/
/*
 * ��������дӦ�ó����ʱ�򣬿�����Ҫ�õ�һЩȫ�ֱ�����
 */
extern QueueHandle_t MQTT_Data_Queue;


//�����û���Ϣ�ṹ��
MQTT_USER_MSG  mqtt_user_msg;

int32_t MQTT_Socket = 0;


void deliverMessage(MQTTString *TopicName,MQTTMessage *msg,MQTT_USER_MSG *mqtt_user_msg);

/************************************************************************
** ��������: MQTT_Connect								
** ��������: ��ʼ���ͻ��˲���¼������
** ��ڲ���: int32_t sock:����������
** ���ڲ���: >=0:���ͳɹ� <0:����ʧ��
** ��    ע: 
************************************************************************/
uint8_t MQTT_Connect(void)
{
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    uint8_t buf[200];
    int buflen = sizeof(buf);
    int len = 0;
    data.clientID.cstring = CLIENT_ID;                   //���
    data.keepAliveInterval = KEEPLIVE_TIME;         //���ֻ�Ծ
    data.username.cstring = USER_NAME;              //�û���
    data.password.cstring = PASSWORD;               //��Կ
    data.MQTTVersion = MQTT_VERSION;                //3��ʾ3.1�汾��4��ʾ3.11�汾
    data.cleansession = 1;
    //��װ��Ϣ
    len = MQTTSerialize_connect((unsigned char *)buf, buflen, &data);
    //������Ϣ
    transport_sendPacketBuffer(buf, len);

    /* �ȴ�������Ӧ */
    if (MQTTPacket_read(buf, buflen, transport_getdata) == CONNACK)
    {
        unsigned char sessionPresent, connack_rc;
        if (MQTTDeserialize_connack(&sessionPresent, &connack_rc, buf, buflen) != 1 || connack_rc != 0)
        {
          PRINT_DEBUG("�޷����ӣ����������: %d��\n", connack_rc);
            return Connect_NOK;
        }
        else 
        {
            PRINT_DEBUG("�û�������Կ��֤�ɹ���MQTT���ӳɹ���\n");
            return Connect_OK;
        }
    }
    else
        PRINT_DEBUG("MQTT��������Ӧ��\n");
        return Connect_NOTACK;
}


/************************************************************************
** ��������: MQTT_PingReq								
** ��������: ����MQTT������
** ��ڲ���: ��
** ���ڲ���: >=0:���ͳɹ� <0:����ʧ��
** ��    ע: 
************************************************************************/
int32_t MQTT_PingReq(int32_t sock)
{
	  int32_t len;
		uint8_t buf[200];
		int32_t buflen = sizeof(buf);	 
		fd_set readfd;
	  struct timeval tv;
	  tv.tv_sec = 5;
	  tv.tv_usec = 0;
	
	  FD_ZERO(&readfd);
	  FD_SET(sock,&readfd);			
	
		len = MQTTSerialize_pingreq(buf, buflen);
		transport_sendPacketBuffer(buf, len);
	
		//�ȴ��ɶ��¼�
		if(select(sock+1,&readfd,NULL,NULL,&tv) == 0)
			return -1;
		
	  //�пɶ��¼�
		if(FD_ISSET(sock,&readfd) == 0)
			return -2;
		
		if(MQTTPacket_read(buf, buflen, transport_getdata) != PINGRESP)
			return -3;
		
		return 0;
	
}


/************************************************************************
** ��������: MQTTSubscribe								
** ��������: ������Ϣ
** ��ڲ���: int32_t sock���׽���
**           int8_t *topic������
**           enum QoS pos����Ϣ����
** ���ڲ���: >=0:���ͳɹ� <0:����ʧ��
** ��    ע: 
************************************************************************/
int32_t MQTTSubscribe(int32_t sock,char *topic,enum QoS pos)
{
	  static uint32_t PacketID = 0;
	  uint16_t packetidbk = 0;
	  int32_t conutbk = 0;
		uint8_t buf[100];
		int32_t buflen = sizeof(buf);
	  MQTTString topicString = MQTTString_initializer;  
		int32_t len;
	  int32_t req_qos,qosbk;
	
		fd_set readfd;
	  struct timeval tv;
	  tv.tv_sec = 2;
	  tv.tv_usec = 0;
	
	  FD_ZERO(&readfd);
	  FD_SET(sock,&readfd);		
	
	  //��������
    topicString.cstring = (char *)topic;
		//��������
	  req_qos = pos;
	
	  //���л�������Ϣ
    len = MQTTSerialize_subscribe(buf, buflen, 0, PacketID++, 1, &topicString, &req_qos);
		//����TCP����
	  if(transport_sendPacketBuffer(buf, len) < 0)
				return -1;
	  
    //�ȴ��ɶ��¼�--�ȴ���ʱ
		if(select(sock+1,&readfd,NULL,NULL,&tv) == 0)
				return -2;
		//�пɶ��¼�--û�пɶ��¼�
		if(FD_ISSET(sock,&readfd) == 0)
				return -3;

		//�ȴ����ķ���--δ�յ����ķ���
		if(MQTTPacket_read(buf, buflen, transport_getdata) != SUBACK)
				return -4;	
		
		//���Ļ�Ӧ��
		if(MQTTDeserialize_suback(&packetidbk,1, &conutbk, &qosbk, buf, buflen) != 1)
				return -5;
		
		//��ⷵ�����ݵ���ȷ��
		if((qosbk == 0x80)||(packetidbk != (PacketID-1)))
				return -6;
		
    //���ĳɹ�
		return 0;
}


/************************************************************************
** ��������: UserMsgCtl						
** ��������: �û���Ϣ��������
** ��ڲ���: MQTT_USER_MSG  *msg����Ϣ�ṹ��ָ��
** ���ڲ���: ��
** ��    ע: 
************************************************************************/
void UserMsgCtl(MQTT_USER_MSG  *msg)
{
		//���ﴦ������ֻ�Ǵ�ӡ���û����������������Լ��Ĵ�����ʽ
//   if(msg->msglenth > 2)    //ֻ�е���Ϣ���ȴ���2 "{}" ��ʱ���ȥ������ 
//   {
      PRINT_DEBUG("*****�յ����ĵ���Ϣ��******\n");
      //���غ�����Ϣ
        if(msg->msglenth > 2)    //ֻ�е���Ϣ���ȴ���2 "{}" ��ʱ���ȥ������ 
   {
      switch(msg->msgqos)
      {
        case 0:
              PRINT_DEBUG("MQTT>>��Ϣ������QoS0\n");
              break;
        case 1:
              PRINT_DEBUG("MQTT>>��Ϣ������QoS1\n");
              break;
        case 2:
              PRINT_DEBUG("MQTT>>��Ϣ������QoS2\n");
              break;
        default:
              PRINT_DEBUG("MQTT>>�������Ϣ����\n");
              break;
      }
      PRINT_DEBUG("MQTT>>��Ϣ���⣺%s\n",msg->topic);	
      PRINT_DEBUG("MQTT>>��Ϣ���ݣ�%s\n",msg->msg);	
      PRINT_DEBUG("MQTT>>��Ϣ���ȣ�%d\n",msg->msglenth);	 

      Proscess(msg->msg);
    }
	  //���������������
	  msg->valid  = 0;
}

/************************************************************************
** ��������: GetNextPackID						
** ��������: ������һ�����ݰ�ID
** ��ڲ���: ��
** ���ڲ���: uint16_t packetid:������ID
** ��    ע: 
************************************************************************/
uint16_t GetNextPackID(void)
{
	 static uint16_t pubpacketid = 0;
	 return pubpacketid++;
}

/************************************************************************
** ��������: mqtt_msg_publish						
** ��������: �û�������Ϣ
** ��ڲ���: MQTT_USER_MSG  *msg����Ϣ�ṹ��ָ��
** ���ڲ���: >=0:���ͳɹ� <0:����ʧ��
** ��    ע: 
************************************************************************/
int32_t MQTTMsgPublish(int32_t sock, char *topic, int8_t qos, uint8_t* msg)
{
    int8_t retained = 0;      //������־λ
    uint32_t msg_len;         //���ݳ���
		uint8_t buf[MSG_MAX_LEN];
		int32_t buflen = sizeof(buf),len;
		MQTTString topicString = MQTTString_initializer;
	  uint16_t packid = 0,packetidbk;
	
		//�������
	  topicString.cstring = (char *)topic;

	  //������ݰ�ID
	  if((qos == QOS1)||(qos == QOS2))
		{ 
			packid = GetNextPackID();
		}
		else
		{
			  qos = QOS0;
			  retained = 0;
			  packid = 0;
		}
     
    msg_len = strlen((char *)msg_len);
    
		//������Ϣ
		len = MQTTSerialize_publish(buf, buflen, 0, qos, retained, packid, topicString, (unsigned char*)msg, msg_len);
		if(len <= 0)
				return -1;
		if(transport_sendPacketBuffer(buf, len) < 0)	
				return -2;	
		
		//�����ȼ�0������Ҫ����
		if(qos == QOS0)
		{
				return 0;
		}
		
		//�ȼ�1
		if(qos == QOS1)
		{
				//�ȴ�PUBACK
			  if(WaitForPacket(sock,PUBACK,5) < 0)
					 return -3;
				return 1;
		}
		//�ȼ�2
		if(qos == QOS2)	
		{
			  //�ȴ�PUBREC
			  if(WaitForPacket(sock,PUBREC,5) < 0)
					 return -3;
			  //����PUBREL
        len = MQTTSerialize_pubrel(buf, buflen,0, packetidbk);
				if(len == 0)
					return -4;
				if(transport_sendPacketBuffer(buf, len) < 0)	
					return -6;			
			  //�ȴ�PUBCOMP
			  if(WaitForPacket(sock,PUBREC,5) < 0)
					 return -7;
				return 2;
		}
		//�ȼ�����
		return -8;
}

/************************************************************************
** ��������: ReadPacketTimeout					
** ��������: ������ȡMQTT����
** ��ڲ���: int32_t sock:����������
**           uint8_t *buf:���ݻ�����
**           int32_t buflen:��������С
**           uint32_t timeout:��ʱʱ��--0-��ʾֱ�Ӳ�ѯ��û��������������
** ���ڲ���: -1������,����--������
** ��    ע: 
************************************************************************/
int32_t ReadPacketTimeout(int32_t sock,uint8_t *buf,int32_t buflen,uint32_t timeout)
{
		fd_set readfd;
	  struct timeval tv;
	  if(timeout != 0)
		{
				tv.tv_sec = timeout;
				tv.tv_usec = 0;
				FD_ZERO(&readfd);
				FD_SET(sock,&readfd); 

				//�ȴ��ɶ��¼�--�ȴ���ʱ
				if(select(sock+1,&readfd,NULL,NULL,&tv) == 0)
						return -1;
				//�пɶ��¼�--û�пɶ��¼�
				if(FD_ISSET(sock,&readfd) == 0)
						return -1;
	  }
		//��ȡTCP/IP�¼�
		return MQTTPacket_read(buf, buflen, transport_getdata);
}


/************************************************************************
** ��������: deliverMessage						
** ��������: ���ܷ�������������Ϣ
** ��ڲ���: MQTTMessage *msg:MQTT��Ϣ�ṹ��
**           MQTT_USER_MSG *mqtt_user_msg:�û����ܽṹ��
**           MQTTString  *TopicName:����
** ���ڲ���: ��
** ��    ע: 
************************************************************************/
void deliverMessage(MQTTString  *TopicName,MQTTMessage *msg,MQTT_USER_MSG *mqtt_user_msg)
{
		//��Ϣ����
		mqtt_user_msg->msgqos = msg->qos;
		//������Ϣ
		memcpy(mqtt_user_msg->msg,msg->payload,msg->payloadlen);
		mqtt_user_msg->msg[msg->payloadlen] = 0;
		//������Ϣ����
		mqtt_user_msg->msglenth = msg->payloadlen;
		//��Ϣ����
		memcpy((char *)mqtt_user_msg->topic,TopicName->lenstring.data,TopicName->lenstring.len);
		mqtt_user_msg->topic[TopicName->lenstring.len] = 0;
		//��ϢID
		mqtt_user_msg->packetid = msg->id;
		//������Ϣ�Ϸ�
		mqtt_user_msg->valid = 1;		
}


/************************************************************************
** ��������: mqtt_pktype_ctl						
** ��������: ���ݰ����ͽ��д���
** ��ڲ���: uint8_t packtype:������
** ���ڲ���: ��
** ��    ע: 
************************************************************************/
void mqtt_pktype_ctl(uint8_t packtype,uint8_t *buf,uint32_t buflen)
{
	  MQTTMessage msg;
		int32_t rc;
	  MQTTString receivedTopic;
	  uint32_t len;
		switch(packtype)
		{
			case PUBLISH:
        //����PUBLISH��Ϣ
        if(MQTTDeserialize_publish(&msg.dup,(int*)&msg.qos, &msg.retained, &msg.id, &receivedTopic,
          (unsigned char **)&msg.payload, &msg.payloadlen, buf, buflen) != 1)
            return;	
        //������Ϣ
        deliverMessage(&receivedTopic,&msg,&mqtt_user_msg);
        
        //��Ϣ������ͬ��������ͬ
        if(msg.qos == QOS0)
        {
           //QOS0-����ҪACK
           //ֱ�Ӵ�������
           UserMsgCtl(&mqtt_user_msg);
           return;
        }
        //����PUBACK��Ϣ
        if(msg.qos == QOS1)
        {
            len =MQTTSerialize_puback(buf,buflen,mqtt_user_msg.packetid);
            if(len == 0)
              return;
            //���ͷ���
            if(transport_sendPacketBuffer(buf,len)<0)
               return;	
            //���غ�����Ϣ
            UserMsgCtl(&mqtt_user_msg); 
            return;												
        }

        //��������2,ֻ��Ҫ����PUBREC�Ϳ�����
        if(msg.qos == QOS2)
        {
           len = MQTTSerialize_ack(buf, buflen, PUBREC, 0, mqtt_user_msg.packetid);			                
           if(len == 0)
             return;
           //���ͷ���
           transport_sendPacketBuffer(buf,len);	
        }		
        break;
			case  PUBREL:				           
        //���������ݣ������ID��ͬ�ſ���
        rc = MQTTDeserialize_ack(&msg.type,&msg.dup, &msg.id, buf,buflen);
        if((rc != 1)||(msg.type != PUBREL)||(msg.id != mqtt_user_msg.packetid))
          return ;
        //�յ�PUBREL����Ҫ��������������
        if(mqtt_user_msg.valid == 1)
        {
           //���غ�����Ϣ
           UserMsgCtl(&mqtt_user_msg);
        }      
        //���л�PUBCMP��Ϣ
        len = MQTTSerialize_pubcomp(buf,buflen,msg.id);	                   	
        if(len == 0)
          return;									
        //���ͷ���--PUBCOMP
        transport_sendPacketBuffer(buf,len);										
        break;
			case   PUBACK://�ȼ�1�ͻ����������ݺ󣬷���������
				break;
			case   PUBREC://�ȼ�2�ͻ����������ݺ󣬷���������
				break;
			case   PUBCOMP://�ȼ�2�ͻ�������PUBREL�󣬷���������
        break;
			default:
				break;
		}
}

/************************************************************************
** ��������: WaitForPacket					
** ��������: �ȴ��ض������ݰ�
** ��ڲ���: int32_t sock:����������
**           uint8_t packettype:������
**           uint8_t times:�ȴ�����
** ���ڲ���: >=0:�ȵ����ض��İ� <0:û�еȵ��ض��İ�
** ��    ע: 
************************************************************************/
int32_t WaitForPacket(int32_t sock,uint8_t packettype,uint8_t times)
{
	  int32_t type;
		uint8_t buf[MSG_MAX_LEN];
	  uint8_t n = 0;
		int32_t buflen = sizeof(buf);
		do
		{
				//��ȡ���ݰ�
				type = ReadPacketTimeout(sock,buf,buflen,2);
			  if(type != -1)
					mqtt_pktype_ctl(type,buf,buflen);
				n++;
		}while((type != packettype)&&(n < times));
		//�յ������İ�
		if(type == packettype)
			 return 0;
		else 
			 return -1;		
}



void Client_Connect(void)
{
    char* host_ip;
  
#if  LWIP_DNS
    ip4_addr_t dns_ip;
    netconn_gethostbyname(HOST_NAME, &dns_ip);
    host_ip = ip_ntoa(&dns_ip);
    PRINT_DEBUG("host name : %s , host_ip : %s\n",HOST_NAME,host_ip);
#else
    host_ip = HOST_NAME;
#endif  
MQTT_START: 
		//������������
		PRINT_DEBUG("1.��ʼ���Ӷ�Ӧ��ƽ̨�ķ�����...\n");
    PRINT_DEBUG("������IP��ַ��%s���˿ںţ�%0d��\n",host_ip,HOST_PORT);
		while(1)
		{
				//���ӷ�����
				MQTT_Socket = transport_open((int8_t*)host_ip,HOST_PORT);
				//������ӷ������ɹ�
				if(MQTT_Socket >= 0)
				{
						PRINT_DEBUG("������ƽ̨�������ɹ���\n");
						break;
				}
				PRINT_DEBUG("������ƽ̨������ʧ�ܣ��ȴ�3���ٳ����������ӣ�\n");
				//�ȴ�3��
				vTaskDelay(3000);
		}

    PRINT_DEBUG("2.MQTT�û�������Կ��֤��½...\n");
    //MQTT�û�������Կ��֤��½
    if(MQTT_Connect() != Connect_OK)
    {
         //����������
         PRINT_DEBUG("MQTT�û�������Կ��֤��½ʧ��...\n");
          //�ر�����
         transport_close();
         goto MQTT_START;	 
    }

		//������Ϣ
		PRINT_DEBUG("3.��ʼ������Ϣ...\n");
//    //������Ϣ

    if(MQTTSubscribe(MQTT_Socket,(char *)TOPIC,QOS1) < 0)
    {
         //����������
         PRINT_DEBUG("�ͻ��˶�����Ϣʧ��...\n");
          //�ر�����
         transport_close();
         goto MQTT_START;	   
    }	
		//����ѭ��
		PRINT_DEBUG("4.��ʼѭ�����ն��ĵ���Ϣ...\n");
}





/************************************************************************
** ��������: mqtt_thread								
** ��������: MQTT����
** ��ڲ���: void *pvParameters���������
** ���ڲ���: ��
** ��    ע: MQTT���Ʋ��裺
**           1.���Ӷ�Ӧ��ƽ̨�ķ�����
**           2.MQTT�û�����Կ��֤��½
**           3.����ָ������
**           4.�ȴ�����������������ϱ���������
************************************************************************/
void mqtt_thread(void *pvParameters)
{
	  uint32_t curtick;
		uint8_t no_mqtt_msg_exchange = 1;
		uint8_t buf[MSG_MAX_LEN];
		int32_t buflen = sizeof(buf);
    int32_t type;
    fd_set readfd;
	  struct timeval tv;      //�ȴ�ʱ��
	  tv.tv_sec = 0;
	  tv.tv_usec = 10;

  
MQTT_START: 
    //��ʼ����
    Client_Connect();
    //��ȡ��ǰ�δ���Ϊ��������ʼʱ��
		curtick = xTaskGetTickCount();
		while(1)
		{
				//���������ݽ���
				no_mqtt_msg_exchange = 1;
			
				//������Ϣ
				FD_ZERO(&readfd);
				FD_SET(MQTT_Socket,&readfd);						  

				//�ȴ��ɶ��¼�
				select(MQTT_Socket+1,&readfd,NULL,NULL,&tv);
				
				//�ж�MQTT�������Ƿ�������
				if(FD_ISSET(MQTT_Socket,&readfd) != 0)
				{
						//��ȡ���ݰ�--ע���������Ϊ0��������
						type = ReadPacketTimeout(MQTT_Socket,buf,buflen,50);
						if(type != -1)
						{
								mqtt_pktype_ctl(type,buf,buflen);
								//���������ݽ���
								no_mqtt_msg_exchange = 0;
								//��ȡ��ǰ�δ���Ϊ��������ʼʱ��
								curtick = xTaskGetTickCount();
						}
				}
        
        //������ҪĿ���Ƕ�ʱ�����������PING��������
        if((xTaskGetTickCount() - curtick) >(KEEPLIVE_TIME/2*1000))
        {
            curtick = xTaskGetTickCount();
            //�ж��Ƿ������ݽ���
            if(no_mqtt_msg_exchange == 0)
            {
               //��������ݽ�������ξͲ���Ҫ����PING��Ϣ
               continue;
            }
            
            if(MQTT_PingReq(MQTT_Socket) < 0)
            {
               //����������
               PRINT_DEBUG("���ͱ��ֻ���pingʧ��....\n");
               goto CLOSE;	 
            }
            
            //�����ɹ�
            PRINT_DEBUG("���ͱ��ֻ���ping��Ϊ�����ɹ�....\n");
            //���������ݽ���
            no_mqtt_msg_exchange = 0;
        }   
		}

CLOSE:
	 //�ر�����
	 transport_close();
	 //�������ӷ�����
	 goto MQTT_START;	
}

void mqtt_send(void *pvParameters)
{
    int32_t ret;
    uint8_t no_mqtt_msg_exchange = 1;
    uint32_t curtick;
    uint8_t res;
    /* ����һ��������Ϣ����ֵ��Ĭ��ΪpdTRUE */
    BaseType_t xReturn = pdTRUE;
    /* ����һ��������Ϣ�ı��� */
//    uint32_t* r_data;	
    DHT11_Data_TypeDef* recv_data;
    //��ʼ��json����
    cJSON* cJSON_Data = NULL;
    cJSON_Data = cJSON_Data_Init();
    double a,b;
MQTT_SEND_START:
  
    while(1)
    {
        
    xReturn = xQueueReceive( MQTT_Data_Queue,    /* ��Ϣ���еľ�� */
                             &recv_data,      /* ���͵���Ϣ���� */
                             3000); /* �ȴ�ʱ�� 3000ms */
      if(xReturn == pdTRUE)
      {
        a = recv_data->temperature;
        b = recv_data->humidity;
//        printf("a = %f,b = %f\n",a,b);
        //��������      
        res = cJSON_Update(cJSON_Data,TEMP_NUM,&a);
        res = cJSON_Update(cJSON_Data,HUM_NUM,&b);
      
        if(UPDATE_SUCCESS == res)
        {
            //�������ݳɹ���
            char* p = cJSON_Print(cJSON_Data);
            //������Ϣ
            ret = MQTTMsgPublish(MQTT_Socket,(char*)TOPIC,QOS0,(uint8_t*)p);
            if(ret >= 0)
            {
                //���������ݽ���
                no_mqtt_msg_exchange = 0;
                //��ȡ��ǰ�δ���Ϊ��������ʼʱ��
                curtick = xTaskGetTickCount();				
            }
            vPortFree(p);
            p = NULL;
        }
        else
          PRINT_DEBUG("update fail\n");
      }
      //������ҪĿ���Ƕ�ʱ�����������PING��������
      if((xTaskGetTickCount() - curtick) >(KEEPLIVE_TIME/2*1000))
      {
          curtick = xTaskGetTickCount();
          //�ж��Ƿ������ݽ���
          if(no_mqtt_msg_exchange == 0)
          {
             //��������ݽ�������ξͲ���Ҫ����PING��Ϣ
             continue;
          }
          
          if(MQTT_PingReq(MQTT_Socket) < 0)
          {
             //����������
             PRINT_DEBUG("���ͱ��ֻ���pingʧ��....\n");
             goto MQTT_SEND_CLOSE;	 
          }
          
          //�����ɹ�
          PRINT_DEBUG("���ͱ��ֻ���ping��Ϊ�����ɹ�....\n");
          //���������ݽ���
          no_mqtt_msg_exchange = 0;
      } 
  }
MQTT_SEND_CLOSE:
	 //�ر�����
	 transport_close(); 
   //��ʼ����
   Client_Connect();
   goto MQTT_SEND_START;
}

void
mqtt_thread_init(void)
{
  sys_thread_new("iperf_client", mqtt_thread, NULL, 1024, 6);
  sys_thread_new("iperf_client", mqtt_send, NULL, 1024, 7);
}

