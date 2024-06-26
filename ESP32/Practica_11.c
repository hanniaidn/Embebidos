#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"
#include <driver/gpio.h>

#include "time.h"
#include "sys/time.h"

#include "driver/mcpwm_prelude.h"
#include "esp_adc/adc_oneshot.h" //Para hacer lecturas Oneshot o continuas

/////////////////servo 
#define SERVO_MIN_PULSEWIDTH_US 500 /* Minimum pulse width in microsecond */
#define SERVO_MAX_PULSEWIDTH_US 2400 /* Maximum pulse width in microsecond */
#define SERVO_MIN_DEGREE 0 /* Minimum angle */
#define SERVO_MAX_DEGREE 180 /* Maximum angle */
#define SERVO_PULSE_GPIO 21 /* GPIO connects to the PWM signal line */
#define SERVO_PULSE_GPIO2 22 /* GPIO connects to the PWM signal line */
#define SERVO_TIMEBASE_RESOLUTION_HZ 1000000 /* 1MHz, 1us per tick */
#define SERVO_TIMEBASE_PERIOD 20000 /* 20000 ticks, 20ms */
/////////////////////////////
#define LEDR 2
#define LEDG 5
#define BUZZER 4
#define SENSOR_IR 18
#define TAG1 "LOG"
/////////////////////////Bluetooth
#define SPP_TAG "SPP_ACCEPTOR_DEMO"
#define SPP_SERVER_NAME "SPP_SERVER"
#define EXAMPLE_DEVICE_NAME "ESP_SPP_ACCEPTOR"
#define SPP_SHOW_DATA 0
#define SPP_SHOW_SPEED 1
#define SPP_SHOW_MODE SPP_SHOW_DATA    /*Choose show mode: show data or speed*/

char *user1 = "Benito";
char *pass1 = "a123bcd";
char *user = "";
char *pass = "";
int angle1=0;
int angle2=0;

esp_err_t init_irs();
void isr_handler(void *args);

int bandera = 0;

int marc_timepo = 0;
bool incorrect_ind = true;

void tarea_paralela(void *pvParameters); 
void mover_servo(void *pvParameters);

///////////////Servo
static const char *TAG = "PWM servo";
char val;

mcpwm_timer_handle_t timer = NULL;
mcpwm_oper_handle_t oper = NULL;
mcpwm_cmpr_handle_t comparator = NULL;
mcpwm_gen_handle_t generator = NULL;
mcpwm_cmpr_handle_t comparator2 = NULL;
mcpwm_gen_handle_t generator2= NULL;

esp_err_t mcpwm_config();
static inline uint32_t angle_to_compare1(int angle1);
static inline uint32_t angle_to_compare2(int angle2);


static const esp_spp_mode_t esp_spp_mode = ESP_SPP_MODE_CB;
static const bool esp_spp_enable_l2cap_ertm = true;

static struct timeval time_new, time_old;
static long data_num = 0;

static const esp_spp_sec_t sec_mask = ESP_SPP_SEC_AUTHENTICATE;
static const esp_spp_role_t role_slave = ESP_SPP_ROLE_SLAVE;

static char *bda2str(uint8_t * bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

static void print_speed(void)
{
    float time_old_s = time_old.tv_sec + time_old.tv_usec / 1000000.0;
    float time_new_s = time_new.tv_sec + time_new.tv_usec / 1000000.0;
    float time_interval = time_new_s - time_old_s;
    float speed = data_num * 8 / time_interval / 1000.0;
    ESP_LOGI(SPP_TAG, "speed(%fs ~ %fs): %f kbit/s" , time_old_s, time_new_s, speed);
    data_num = 0;
    time_old.tv_sec = time_new.tv_sec;
    time_old.tv_usec = time_new.tv_usec;
}

esp_err_t pin_initialize(){

    gpio_reset_pin(LEDR);
    gpio_set_direction(LEDR, GPIO_MODE_OUTPUT);

    gpio_reset_pin(LEDG);
    gpio_set_direction(LEDG, GPIO_MODE_OUTPUT);

    gpio_reset_pin(BUZZER);
    gpio_set_direction(BUZZER, GPIO_MODE_OUTPUT);

    gpio_reset_pin(SENSOR_IR);
    gpio_set_direction(SENSOR_IR, GPIO_MODE_INPUT);
    
    return ESP_OK;
}

//servo inicio

//servo
esp_err_t mcpwm_config(){
    ESP_LOGI(TAG, "Create timer and operator");

    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = SERVO_TIMEBASE_RESOLUTION_HZ,
        .period_ticks = SERVO_TIMEBASE_PERIOD,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));

    mcpwm_operator_config_t operator_config = {
        .group_id = 0, // operator must be in the same group to the timer
    };
    ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &oper));

    ESP_LOGI(TAG, "Connect timer and operator");
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

    ESP_LOGI(TAG, "Create comparator and generator from the operator");
    
    mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &comparator));
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &comparator2));

    
    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = SERVO_PULSE_GPIO,
    };
     mcpwm_generator_config_t generator_config2 = {
        .gen_gpio_num = SERVO_PULSE_GPIO2,
    };
    
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config, &generator));
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config2, &generator2));

    // set the initial compare value, so that the servo will spin to the center position
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, angle_to_compare1(0)));
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator2, angle_to_compare2(0)));

    ESP_LOGI(TAG, "Set generator action on timer and compare event");
    // go high on counter empty
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    // go low on compare threshold
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(generator, MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator, MCPWM_GEN_ACTION_LOW)));
    
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator2, MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    // go low on compare threshold
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(generator2, MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparator2, MCPWM_GEN_ACTION_LOW)));

    ESP_LOGI(TAG, "Enable and start timer");
    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
    

    return ESP_OK;
}

static inline uint32_t angle_to_compare2(int angle1){
    return (angle1 - SERVO_MIN_DEGREE) * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) / (SERVO_MAX_DEGREE - SERVO_MIN_DEGREE) + SERVO_MIN_PULSEWIDTH_US;
}
static inline uint32_t angle_to_compare1(int angle2){
    return (angle2 - SERVO_MIN_DEGREE) * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) / (SERVO_MAX_DEGREE - SERVO_MIN_DEGREE) + SERVO_MIN_PULSEWIDTH_US;
}


static void esp_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    char bda_str[18] = {0};

    switch (event) {
    case ESP_SPP_INIT_EVT:
        if (param->init.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(SPP_TAG, "ESP_SPP_INIT_EVT");
            esp_spp_start_srv(sec_mask, role_slave, 0, SPP_SERVER_NAME);
        } else {
            ESP_LOGE(SPP_TAG, "ESP_SPP_INIT_EVT status:%d", param->init.status);
        }
        break;
    case ESP_SPP_DISCOVERY_COMP_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_DISCOVERY_COMP_EVT");
        break;
    case ESP_SPP_OPEN_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_OPEN_EVT");
        break;
    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_CLOSE_EVT status:%d handle:%"PRIu32" close_by_remote:%d", param->close.status,
                 param->close.handle, param->close.async);
        break;
    case ESP_SPP_START_EVT:
        if (param->start.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(SPP_TAG, "ESP_SPP_START_EVT handle:%"PRIu32" sec_id:%d scn:%d", param->start.handle, param->start.sec_id,
                     param->start.scn);
            esp_bt_dev_set_device_name(EXAMPLE_DEVICE_NAME);
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        } else {
            ESP_LOGE(SPP_TAG, "ESP_SPP_START_EVT status:%d", param->start.status);
        }
        break;
    case ESP_SPP_CL_INIT_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_CL_INIT_EVT");
        break;
    case ESP_SPP_DATA_IND_EVT:

#if (SPP_SHOW_MODE == SPP_SHOW_DATA)

        char *data_str = (char*)malloc(param->data_ind.len + 1);

        memcpy(data_str, param->data_ind.data, param->data_ind.len);

        data_str[(param->data_ind.len) - 2] = '\0';
 
 
        ESP_LOGI(SPP_TAG,"Data recived: %s",data_str);

        /*
        for(int i=0; i < strlen(data_str); i++){
            printf("%c\n", data_str[i]);
        }*/

        printf("Tarea ejecutandose en el nucleo %d\n", xPortGetCoreID());


        if( strlen(data_str) !=  0 && marc_timepo == 0 && gpio_get_level(SENSOR_IR) == 1){
            marc_timepo = 1;
            user = data_str;
            ESP_LOGI(SPP_TAG,"111: %s %i",user, marc_timepo);
        }
        else if(strlen(data_str) !=  0 && marc_timepo == 1 && gpio_get_level(SENSOR_IR) == 1){
            marc_timepo = 0;
            pass = data_str;
            ESP_LOGI(SPP_TAG,"222: %s %i",pass, marc_timepo);
            xTaskCreatePinnedToCore(tarea_paralela, "Tarea Paralela", 2048, NULL, 1, NULL, 1); //nucleo 1
        }

#else
        gettimeofday(&time_new, NULL);
        data_num += param->data_ind.len;
        if (time_new.tv_sec - time_old.tv_sec >= 3) {
            print_speed();
        }
#endif
        break;
    case ESP_SPP_CONG_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_CONG_EVT");
        break;
    case ESP_SPP_WRITE_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_WRITE_EVT");
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_SRV_OPEN_EVT status:%d handle:%"PRIu32", rem_bda:[%s]", param->srv_open.status,
                 param->srv_open.handle, bda2str(param->srv_open.rem_bda, bda_str, sizeof(bda_str)));
        gettimeofday(&time_old, NULL);
        break;
    case ESP_SPP_SRV_STOP_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_SRV_STOP_EVT");
        break;
    case ESP_SPP_UNINIT_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_UNINIT_EVT");
        break;
    default:
        break;
    }
}

void esp_bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    char bda_str[18] = {0};

    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:{
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(SPP_TAG, "authentication success: %s bda:[%s]", param->auth_cmpl.device_name,
                     bda2str(param->auth_cmpl.bda, bda_str, sizeof(bda_str)));
        } else {
            ESP_LOGE(SPP_TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
        }
        break;
    }
    case ESP_BT_GAP_PIN_REQ_EVT:{
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
        if (param->pin_req.min_16_digit) {
            ESP_LOGI(SPP_TAG, "Input pin code: 0000 0000 0000 0000");
            esp_bt_pin_code_t pin_code = {0};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        } else {
            ESP_LOGI(SPP_TAG, "Input pin code: 1234");
            esp_bt_pin_code_t pin_code;
            pin_code[0] = '1';
            pin_code[1] = '2';
            pin_code[2] = '3';
            pin_code[3] = '4';
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;
    }

#if (CONFIG_BT_SSP_ENABLED == true)
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %"PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%"PRIu32, param->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;
#endif

    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode:%d bda:[%s]", param->mode_chg.mode,
                 bda2str(param->mode_chg.bda, bda_str, sizeof(bda_str)));
        break;

    default: {
        ESP_LOGI(SPP_TAG, "event: %d", event);
        break;
    }
    }
    return;
}

void app_main(void)
{
    mcpwm_config();
    pin_initialize();
    init_irs();
   
    char bda_str[18] = {0};
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s initialize bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s enable bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bt_gap_register_callback(esp_bt_gap_cb)) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s gap register failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_spp_register_callback(esp_spp_cb)) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s spp register failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    esp_spp_cfg_t bt_spp_cfg = {
        .mode = esp_spp_mode,
        .enable_l2cap_ertm = esp_spp_enable_l2cap_ertm,
        .tx_buffer_size = 0, /* Only used for ESP_SPP_MODE_VFS mode */
    };
    if ((ret = esp_spp_enhanced_init(&bt_spp_cfg)) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s spp init failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

#if (CONFIG_BT_SSP_ENABLED == true)
    /* Set default parameters for Secure Simple Pairing */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
#endif

    /*
     * Set default parameters for Legacy Pairing
     * Use variable pin, input pin code when pairing
     */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    ESP_LOGI(SPP_TAG, "Own address:[%s]", bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));

}


void tarea_paralela(void *pvParameters) {

    //Add delay, since it takes time for servo to rotate, usually 200ms/60degree rotation under 5V power supply 
    // Codigo de la tarea
    if(gpio_get_level(SENSOR_IR)==1){
    printf("Tarea ejecutandose en el nucleo %d\n", xPortGetCoreID());
    if( strlen(user) != 0 &&  strlen(pass) != 0 ){
                if (strcmp(user,user1) != 0 || strcmp(pass,pass1) != 0){
                    incorrect_ind = false;
                    printf("Resultado de la comparacion Pass: %d\n", strcmp(pass, pass1));
                    printf("Resultado de la comparacion User: %d\n", strcmp(user, user1));

                    printf("Resultado len pass: %d\n", strlen(pass));
                    printf("Resultado len pass1: %d\n", strlen(pass1));

                    printf("Resultado user: %d\n", strlen(user));
                    printf("Resultado user1: %d\n", strlen(user1));

                }
                
                for(int l = 0; l < strlen(pass1); l++){
                    printf("%c\n", pass1[l]);
                }
                printf("----\n");
                for(int j = 0; j < strlen(pass); j++){
                    printf("%c\n", pass[j]);
                }
                
                for(int j=0; j<5; j++){

                    switch (incorrect_ind)
                    {
                    case false:

                        ESP_LOGE(TAG1, "error usuario/contraseña");

                        gpio_set_level(LEDR, 1);
                        gpio_set_level(BUZZER, 1);
                        vTaskDelay(pdMS_TO_TICKS(500));
                        gpio_set_level(BUZZER, 0);
                        gpio_set_level(LEDR, 0);

                        break;

                    case true:

                        ESP_LOGI(TAG1, "Acceso Correcto");

                        gpio_set_level(LEDG, 1);
                        gpio_set_level(BUZZER, 1);
                        vTaskDelay(pdMS_TO_TICKS(500));

                        break;
                
                    }

                }

                user = "";
                pass = "";
                incorrect_ind = true;
                bandera = 0;
                    
            }
    }
    gpio_set_level(LEDG, 0);
    gpio_set_level(BUZZER, 0);
    vTaskDelete(NULL);
}

//////////////parametros Servo
void mover_servo(void *pvParameters){

    if(strlen(user) == 0 && strlen(pass)==0 && gpio_get_level(SENSOR_IR)==1 && bandera == 0){

        bandera = 1;
        for(int i=0;i<=10;i++){
        vTaskDelay(pdMS_TO_TICKS(500));
        angle1=18*i;
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator, angle_to_compare1(angle1)));
        
        vTaskDelay(pdMS_TO_TICKS(500));
        angle2=60;
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator2, angle_to_compare2(angle2)));
        vTaskDelay(pdMS_TO_TICKS(300));
        angle2=0;
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(comparator2, angle_to_compare2(angle2)));
        vTaskDelay(pdMS_TO_TICKS(500));
        
        }
    }

    vTaskDelete(NULL);
}

////////////////////Interrupcion
esp_err_t init_irs() {

    gpio_config_t pGPIOConfig;
    pGPIOConfig.pin_bit_mask = (1ULL << SENSOR_IR);
    pGPIOConfig.mode = GPIO_MODE_DEF_INPUT; //incializa el boton desde aqui 
    pGPIOConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    pGPIOConfig.pull_down_en = GPIO_PULLUP_DISABLE;
    pGPIOConfig.intr_type = GPIO_INTR_POSEDGE; //flanco de bajada 

    gpio_config(&pGPIOConfig);

    gpio_install_isr_service(0); //habilita global interrupts
    gpio_isr_handler_add(SENSOR_IR, isr_handler, NULL); //pin y funcion a ejecutar

    return ESP_OK;

}

void isr_handler(void *args){

    xTaskCreatePinnedToCore(mover_servo, "Servo", 2048, NULL, 1, NULL, 1);
    
}
