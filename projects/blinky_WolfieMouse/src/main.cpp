// KB library
#include "kb_tick.h"
#include "kb_trace.h"
#include "kb_gpio.h"
#include "kb_HCMS-290X_display.h"
#include "kb_terminal.h"
#include "kb_timer.h"
#include "kb_TCA9545A_i2c_mux.h"
#include "kb_VL6180X_range_finder.h"
#include "kb_adc.h"

// FreeRTOS
#include "FreeRTOS.h"
#include "portable.h" // TODO: Delete
#include "task.h"
#include "timers.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"

// User config
#include "system_config.h"
#include "encoder.h"
#include "motor.h"

// Algorithm
#include "pid.h"

// Define message base. This is used for KB_DEBUG_MSG()
#ifdef KB_MSG_BASE
	#undef KB_MSG_BASE
	#define KB_MSG_BASE "Main"
#endif

/*******************************************************************************
 * Function declarations
 ******************************************************************************/
// Event declarations
static void on_b1_pressed(void);
static void on_b2_pressed(void);

// Task declarations
static void task_main(void *pvParameters);
static void task_blinky(void *pvParameters);
static void task_range(void *pvParameters);

/*******************************************************************************
 * local variables
 ******************************************************************************/

/*******************************************************************************
 * Global variables
 ******************************************************************************/
// Task Handlers
TaskHandle_t task_main_handler;
TaskHandle_t task_blinky_handler;
TaskHandle_t task_range_handler;

// Mutex Handlers
SemaphoreHandle_t mutex_range;

/*
 * These global varables are located in peripherals.c
 */
// range sensor value
extern volatile uint16_t range_L, range_R, range_F, range_FL, range_FR;
// Encoder value
extern volatile int32_t steps_L, steps_R, steps_L_old, steps_R_old;
extern volatile int32_t speed_L, speed_R, speed_D;

// PID handler
extern pid_handler_t pid_T;
extern pid_handler_t pid_R;

// ADC peripheral object
extern kb_adc_t adc_L;
extern kb_adc_t adc_R;
extern kb_adc_t adc_F;

extern int is_pid_T_running;
extern int is_pid_R_running;
static void stop_all(void);

/*******************************************************************************
 * Main function
 ******************************************************************************/
int main(void)
{
	/* initialize clock and system configuration */
    system_init();

    /* Initialize all configured peripherals */
    peripheral_init();

    kb_gpio_init_t GPIO_InitStruct;

    /* Initial LED Display message */
    hcms_290x_matrix("STRT");

    /* Encoder test reading */
    int left = encoder_left_count();
    int right = encoder_right_count();
    KB_DEBUG_MSG("left encoder: %d\n", left);
    KB_DEBUG_MSG("right encoder: %d\n", right);

    /* Wait for 3 sec */
    for (int i = 0; i < 6; i++) {
        kb_gpio_toggle(LED4_PORT, LED4_PIN);
        kb_delay_ms(500);
    }

    /* Motor test running */
    is_pid_R_running = 1;
    pid_input_setpoint(&pid_T, 18);

    /* Set Button Pressed Events */
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = PULLUP;

    kb_gpio_isr_enable(B1_PORT, B1_PIN, &GPIO_InitStruct, FALLING_EDGE);
    kb_gpio_isr_register(B1_PORT, B1_PIN, on_b1_pressed);

    kb_gpio_isr_enable(B2_PORT, B2_PIN, &GPIO_InitStruct, FALLING_EDGE);
    kb_gpio_isr_register(B2_PORT, B2_PIN, on_b2_pressed);

    /* Test blinking */
    kb_gpio_toggle(LED4_PORT, LED4_PIN);
    kb_delay_ms(4000);
    kb_gpio_toggle(LED4_PORT, LED4_PIN);

    /* Test message */
    trace_puts("Hello ARM World!");
    kb_terminal_puts("Hello World!\n");


    /* Motor test running done */
    stop_all();


    /* Mutex creation */
    mutex_range = xSemaphoreCreateMutex();

    /* Task creation and definition */
    BaseType_t result;
    result = xTaskCreate(
            task_blinky,            /* Pointer to the function that implements the task */
            "Blinky",               /* Text name for the task. This is to facilitate debugging only. It is not used in the scheduler */
            configMINIMAL_STACK_SIZE+500, /* Stack depth in words */
            NULL,                   /* Pointer to a task parameters */
            1,                      /* The task priority */
            &task_blinky_handler);  /* Pointer of its task handler, if you don't want to use, you can leave it NULL */
    if (result != pdPASS) {
        KB_DEBUG_ERROR("Creating task failed!!");
    }

    result = xTaskCreate(
            task_range,
            "Finder",
            configMINIMAL_STACK_SIZE+500,
            NULL,
            configMAX_PRIORITIES-1,
            &task_range_handler);
    if (result != pdPASS) {
        KB_DEBUG_ERROR("Creating task failed!!");
    }

    result = xTaskCreate(
            task_main,
            "Main",
            configMINIMAL_STACK_SIZE+2500,
            NULL,
            configMAX_PRIORITIES-1,
            &task_main_handler);
    if (result != pdPASS) {
        KB_DEBUG_ERROR("Creating task failed!!");
    }

    /* Do not put delay function in this section!
     * Because xTaskCreate will stop systick until the scheduler called */
    // TODO: check if it is true
    /* Start scheduler */
    vTaskStartScheduler();
    while (1) {
    }
}

/*******************************************************************************
 * Tasks
 ******************************************************************************/
static void task_main(void *pvParameters)
{
    portTickType xLastWakeTime;
    /* Initialize xLastWakeTime for vTaskDelayUntil */
    /* This variable is updated every vTaskDelayUntil is called */
    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        /* Call this Task every 1 second */
        vTaskDelayUntil(&xLastWakeTime, (1000 / portTICK_RATE_MS));
    }
    /* It never goes here, but the task should be deleted when it reached here */
    vTaskDelete(NULL);
}

static void task_range(void *pvParameters)
{
    portTickType xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();

    while (1) {
        xSemaphoreTake(mutex_range, portMAX_DELAY); /* Take Mutext */
        //range_left = range_finder_get(left_side);
        //range_right = range_finder_get(right_side);
        //range_front_left = range_finder_get(left_front);
        //range_front_right = range_finder_get(right_front);
        //range_front = (range_front_left + range_front_right) / 2
        xSemaphoreGive(mutex_range);
        /* Call this Task every 50ms */
        vTaskDelayUntil(&xLastWakeTime, (50 / portTICK_RATE_MS));
    }
}

static void task_blinky(void *pvParameters)
{
    portTickType xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();

    uint32_t seconds = 0;

    while (1) {
        kb_gpio_toggle(LED1_PORT, LED1_PIN);
        kb_gpio_toggle(LED2_PORT, LED2_PIN);
        kb_gpio_toggle(LED3_PORT, LED3_PIN);
        kb_gpio_toggle(LED4_PORT, LED4_PIN);
#ifdef KB_BLACKWOLF
        kb_gpio_toggle(LED5_PORT, LED5_PIN);
        kb_gpio_toggle(LED6_PORT, LED6_PIN);
#endif
        kb_terminal_puts("Blink!\n");

        hcms_290x_int(seconds);
        ++seconds;        // Count seconds on the trace device.
        trace_printf("Second %u\n", seconds);

        // Print speed
        KB_DEBUG_MSG("left encoder: %d\n", steps_L);
        KB_DEBUG_MSG("right encoder: %d\n", steps_R);
        KB_DEBUG_MSG("left speed: %d\n", speed_L);
        KB_DEBUG_MSG("right speed: %d\n", speed_R);

        // Print range
        kb_gpio_set(EMITTER_SIDES_PORT, EMITTER_SIDES_PIN, GPIO_PIN_RESET);
        KB_DEBUG_MSG("left range: %d\n", range_L);
        KB_DEBUG_MSG("right range: %d\n", range_R);

        // range front too
        kb_gpio_set(EMITTER_FL_PORT, EMITTER_FL_PIN, GPIO_PIN_SET);
        kb_delay_us(60);
        range_F = kb_adc_measure(&adc_F);
        kb_gpio_set(EMITTER_FL_PORT, EMITTER_FL_PIN, GPIO_PIN_RESET);
        KB_DEBUG_MSG("Front range: %d\n", range_F);

        /* Call this Task every 1000ms */
        vTaskDelayUntil(&xLastWakeTime, (1000 / portTICK_RATE_MS));
    }
    vTaskDelete(NULL);
}

/*******************************************************************************
 * Event handlers
 ******************************************************************************/
static void on_b1_pressed(void)
{
    trace_puts("B1 Pressed\n");
    return;
}

static void on_b2_pressed(void)
{
    trace_puts("B2 Pressed\n");
    return;
}


static void stop_all(void)
{
    is_pid_T_running = 0;
    is_pid_R_running = 0;
    motor_speed_percent(CH_BOTH, 0);
    motor_start(CH_BOTH);
    pid_input_setpoint(&pid_T, 0);
    pid_input_setpoint(&pid_R, 0);
}