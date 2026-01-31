/*******************************************************************************
 * File: examples/queue_demo/main.c
 * Description: Simple Producer-Consumer Queue Demonstration
 * Author: Student
 * Date: 2025
 ******************************************************************************/

#include "VRTOS.h"
#include "config.h"
#include "log.h"
#include "queue.h"
#include "stm32f4xx_hal.h" // IWYU pragma: keep
#include "task.h"

/**
 * @file main.c
 * @brief Producer-Consumer Queue Demonstration
 *
 * This example demonstrates:
 * - Basic queue operations (send/receive)
 * - Multiple producers and consumers
 * - Blocking behavior when queue is full/empty
 * - Priority-based task scheduling with queues
 * 
 * Scenario: Simulated sensor data processing system
 * - Temperature sensors (producers) generate readings
 * - Data processors (consumers) analyze readings
 * - LED blinks when processing occurs
 */

/* =================== Hardware Configuration =================== */

/* LED Configuration for STM32F446RE Nucleo */
#define LED_PORT     GPIOA
#define LED_PIN      5 /* PA5 - User LED (LD2) */
#define LED_PIN_MASK (1U << LED_PIN)

/* =================== Task Priorities =================== */

#define HEARTBEAT_PRIORITY       (1U) /* Lowest - just blinks LED */
#define TEMP_SENSOR_1_PRIORITY   (3U) /* Sensor tasks */
#define TEMP_SENSOR_2_PRIORITY   (3U)
#define PRESSURE_SENSOR_PRIORITY (4U) /* Higher priority sensor */
#define DATA_PROCESSOR_PRIORITY  (5U) /* High priority consumer */
#define DISPLAY_TASK_PRIORITY    (2U) /* Low priority display */
#define MONITOR_TASK_PRIORITY    (6U) /* Highest - monitors system */

/* =================== Timing Configuration =================== */

#define HEARTBEAT_INTERVAL_MS   (1000U)
#define TEMP_SENSOR_1_RATE_MS   (500U)  /* Fast sensor */
#define TEMP_SENSOR_2_RATE_MS   (1500U) /* Slow sensor */
#define PRESSURE_SENSOR_RATE_MS (2000U)
#define PROCESSOR_CHECK_MS      (100U)
#define DISPLAY_UPDATE_MS       (3000U)
#define MONITOR_INTERVAL_MS     (5000U)

/* =================== Data Structures =================== */

/**
 * @brief Sensor data packet
 */
typedef struct
{
    uint8_t  sensor_id; /* Which sensor generated this */
    uint16_t value;     /* Sensor reading */
    uint32_t timestamp; /* When reading was taken */
    char     unit[8];   /* e.g., "°C", "kPa" */
} sensor_data_t;

/**
 * @brief Statistics for monitoring
 */
typedef struct
{
    uint32_t readings_generated;
    uint32_t readings_processed;
    uint32_t readings_dropped;
    uint32_t queue_full_count;
    uint32_t processor_blocked_count;
} system_stats_t;

/* =================== Global Variables =================== */

static rtos_queue_handle_t g_sensor_queue;
static system_stats_t      g_stats          = {0};
static volatile bool       g_system_running = true;

/* HAL handles */
void        SystemClock_Config(void);
static void MX_GPIO_Init(void);

/* =================== LED Control =================== */

/**
 * @brief Toggle LED state
 */
static void led_toggle(void)
{
    if (LED_PORT->ODR & LED_PIN_MASK)
    {
        LED_PORT->BSRR = LED_PIN_MASK << 16; /* Reset bit */
    }
    else
    {
        LED_PORT->BSRR = LED_PIN_MASK; /* Set bit */
    }
}

/**
 * @brief Set LED state
 */
static void led_set(bool on)
{
    if (on)
    {
        LED_PORT->BSRR = LED_PIN_MASK; /* Set bit */
    }
    else
    {
        LED_PORT->BSRR = LED_PIN_MASK << 16; /* Reset bit */
    }
}

/* =================== System Failure Handler =================== */

/**
 * @brief Indicate system failure by blinking LED rapidly
 */
__attribute__((__noreturn__)) static void indicate_system_failure(void)
{
    const uint32_t    delay_cycles = SystemCoreClock / 50;
    volatile uint32_t counter;

    while (1)
    {
        led_toggle();
        for (counter = 0; counter < delay_cycles; counter++)
        {
            __NOP();
        }
    }
}

/* =================== Producer Tasks (Sensors) =================== */

/**
 * @brief Simulated temperature sensor reading
 */
static uint16_t simulate_temperature_reading(uint8_t sensor_id)
{
    static uint16_t base_temp[2] = {2000, 2500}; /* 20.0°C, 25.0°C */
    static int16_t  drift[2]     = {1, -1};

    /* Simple simulation: temperature drifts slowly */
    base_temp[sensor_id] += drift[sensor_id];

    /* Reverse drift at boundaries */
    if (base_temp[sensor_id] > 3000)
        drift[sensor_id] = -1; /* 30°C max */
    if (base_temp[sensor_id] < 1500)
        drift[sensor_id] = 1; /* 15°C min */

    return base_temp[sensor_id];
}

/**
 * @brief Temperature Sensor 1 Task (Fast producer)
 */
static void temp_sensor_1_task(void *param)
{
    (void) param;
    const uint8_t sensor_id = 1;

    log_info("[TEMP_1] Temperature sensor 1 started (rate: %lums)", (unsigned long) TEMP_SENSOR_1_RATE_MS);

    while (g_system_running)
    {
        /* Generate sensor reading */
        sensor_data_t reading;
        reading.sensor_id = sensor_id;
        reading.value     = simulate_temperature_reading(0);
        reading.timestamp = rtos_get_tick_count();
        snprintf(reading.unit, sizeof(reading.unit), "C");

        /* Try to send to queue with timeout */
        rtos_status_t status = rtos_queue_send(g_sensor_queue, &reading, 100);

        if (status == RTOS_SUCCESS)
        {
            g_stats.readings_generated++;
            log_debug("[TEMP_1] Reading sent: %u.%u°C", reading.value / 100, reading.value % 100);
        }
        else if (status == RTOS_ERROR_TIMEOUT)
        {
            g_stats.readings_dropped++;
            g_stats.queue_full_count++;
            log_info("[TEMP_1] Queue full - reading dropped");
        }
        else
        {
            log_error("[TEMP_1] Send error: %d", status);
        }

        /* Sensor sampling rate */
        rtos_delay_ms(TEMP_SENSOR_1_RATE_MS);
    }
}

/**
 * @brief Temperature Sensor 2 Task (Slow producer)
 */
static void temp_sensor_2_task(void *param)
{
    (void) param;
    const uint8_t sensor_id = 2;

    log_info("[TEMP_2] Temperature sensor 2 started (rate: %lums)", (unsigned long) TEMP_SENSOR_2_RATE_MS);

    while (g_system_running)
    {
        /* Generate sensor reading */
        sensor_data_t reading;
        reading.sensor_id = sensor_id;
        reading.value     = simulate_temperature_reading(1);
        reading.timestamp = rtos_get_tick_count();
        snprintf(reading.unit, sizeof(reading.unit), "C");

        /* Try to send to queue - block longer since it's slower */
        rtos_status_t status = rtos_queue_send(g_sensor_queue, &reading, 500);

        if (status == RTOS_SUCCESS)
        {
            g_stats.readings_generated++;
            log_debug("[TEMP_2] Reading sent: %u.%u°C", reading.value / 100, reading.value % 100);
        }
        else if (status == RTOS_ERROR_TIMEOUT)
        {
            g_stats.readings_dropped++;
            g_stats.queue_full_count++;
            log_info("[TEMP_2] Queue full - reading dropped");
        }
        else
        {
            log_error("[TEMP_2] Send error: %d", status);
        }

        rtos_delay_ms(TEMP_SENSOR_2_RATE_MS);
    }
}

/**
 * @brief Simulated pressure sensor reading
 */
static uint16_t simulate_pressure_reading(void)
{
    static uint16_t pressure = 10130; /* 101.30 kPa */
    static int16_t  change   = 1;

    pressure += change;
    if (pressure > 10200)
        change = -1; /* 102.00 kPa max */
    if (pressure < 10100)
        change = 1; /* 101.00 kPa min */

    return pressure;
}

/**
 * @brief Pressure Sensor Task (High priority producer)
 */
static void pressure_sensor_task(void *param)
{
    (void) param;
    const uint8_t sensor_id = 3;

    log_info("[PRESSURE] Pressure sensor started (rate: %lums)", (unsigned long) PRESSURE_SENSOR_RATE_MS);

    while (g_system_running)
    {
        /* Generate sensor reading */
        sensor_data_t reading;
        reading.sensor_id = sensor_id;
        reading.value     = simulate_pressure_reading();
        reading.timestamp = rtos_get_tick_count();
        snprintf(reading.unit, sizeof(reading.unit), "kPa");

        /* High priority - wait longer if needed */
        rtos_status_t status = rtos_queue_send(g_sensor_queue, &reading, 1000);

        if (status == RTOS_SUCCESS)
        {
            g_stats.readings_generated++;
            log_debug("[PRESSURE] Reading sent: %u.%u %s", reading.value / 100, reading.value % 100, reading.unit);
        }
        else if (status == RTOS_ERROR_TIMEOUT)
        {
            g_stats.readings_dropped++;
            g_stats.queue_full_count++;
            log_info("[PRESSURE] Queue full - critical reading dropped!");
        }
        else
        {
            log_error("[PRESSURE] Send error: %d", status);
        }

        rtos_delay_ms(PRESSURE_SENSOR_RATE_MS);
    }
}

/* =================== Consumer Tasks (Data Processing) =================== */

/**
 * @brief Process sensor data (simulate some computation)
 */
static void process_sensor_data(const sensor_data_t *data)
{
    /* Flash LED to show activity */
    led_set(true);

    /* Simulate processing time */
    volatile uint32_t dummy = 0;
    for (uint32_t i = 0; i < 10000; i++)
    {
        dummy += i;
    }
    (void) dummy;

    led_set(false);
}

/**
 * @brief Data Processor Task (Consumer)
 */
static void data_processor_task(void *param)
{
    (void) param;
    sensor_data_t reading;

    log_info("[PROCESSOR] Data processor started");

    while (g_system_running)
    {
        /* Wait for sensor data (block indefinitely) */
        rtos_status_t status = rtos_queue_receive(g_sensor_queue, &reading, RTOS_MAX_DELAY);

        if (status == RTOS_SUCCESS)
        {
            g_stats.readings_processed++;

            /* Log received data */
            log_info("[PROCESSOR] Sensor %u: %u.%u%s [age: %lu ticks]", reading.sensor_id, reading.value / 100,
                     reading.value % 100, reading.unit, (unsigned long) (rtos_get_tick_count() - reading.timestamp));

            /* Process the data */
            process_sensor_data(&reading);

            /* Check for anomalies */
            if (reading.sensor_id <= 2 && reading.value > 2800)
            {
                log_info("[PROCESSOR] ⚠ WARNING: High temperature detected!");
            }
            if (reading.sensor_id == 3 && reading.value > 10180)
            {
                log_info("[PROCESSOR] ⚠ WARNING: High pressure detected!");
            }
        }
        else if (status == RTOS_ERROR_TIMEOUT)
        {
            /* Shouldn't happen with RTOS_MAX_DELAY */
            g_stats.processor_blocked_count++;
            log_info("[PROCESSOR] Unexpected timeout");
        }
        else
        {
            log_error("[PROCESSOR] Receive error: %d", status);
        }
    }
}

/**
 * @brief Display Task (Low priority consumer with timeout)
 */
static void display_task(void *param)
{
    (void) param;
    sensor_data_t reading;

    log_info("[DISPLAY] Display task started");

    while (g_system_running)
    {
        /* Try to get data with short timeout */
        rtos_status_t status = rtos_queue_receive(g_sensor_queue, &reading, 50);

        if (status == RTOS_SUCCESS)
        {
            /* Display update */
            log_debug("[DISPLAY] Update: Sensor %u = %u.%u%s", reading.sensor_id, reading.value / 100,
                      reading.value % 100, reading.unit);
        }
        else if (status == RTOS_ERROR_TIMEOUT)
        {
            /* No data available - that's fine */
            log_debug("[DISPLAY] No data to display");
        }

        /* Update rate independent of data availability */
        rtos_delay_ms(DISPLAY_UPDATE_MS);
    }
}

/* =================== Monitoring Tasks =================== */

/**
 * @brief System Monitor Task
 */
static void monitor_task(void *param)
{
    (void) param;

    log_info("[MONITOR] System monitor started");

    /* Wait for system to stabilize */
    rtos_delay_ms(2000);

    while (g_system_running)
    {
        /* Print system statistics */
        uint32_t queue_count  = rtos_queue_messages_waiting(g_sensor_queue);
        uint32_t queue_spaces = rtos_queue_spaces_available(g_sensor_queue);

        log_info("=== System Status ===");
        log_info("Queue: %lu/%lu items", (unsigned long) queue_count, (unsigned long) (queue_count + queue_spaces));
        log_info("Generated: %lu readings", (unsigned long) g_stats.readings_generated);
        log_info("Processed: %lu readings", (unsigned long) g_stats.readings_processed);
        log_info("Dropped: %lu readings", (unsigned long) g_stats.readings_dropped);
        log_info("Queue full events: %lu", (unsigned long) g_stats.queue_full_count);

        /* Calculate processing efficiency */
        if (g_stats.readings_generated > 0)
        {
            uint32_t efficiency = (g_stats.readings_processed * 100) / g_stats.readings_generated;
            log_info("Efficiency: %lu%%", (unsigned long) efficiency);
        }

        log_info("====================\n");

        rtos_delay_ms(MONITOR_INTERVAL_MS);
    }
}

/**
 * @brief Heartbeat Task (Visual feedback)
 */
static void heartbeat_task(void *param)
{
    (void) param;

    log_info("[HEARTBEAT] Heartbeat task started");

    while (g_system_running)
    {
        /* Quick LED pulse to show system is alive */
        led_set(true);
        rtos_delay_ms(50);
        led_set(false);

        rtos_delay_ms(HEARTBEAT_INTERVAL_MS - 50);
    }
}

/* =================== Main Function =================== */

/**
 * @brief Main function
 */
__attribute__((__noreturn__)) int main(void)
{
    rtos_status_t status;

    /* System initializations */
    SCB->VTOR = FLASH_BASE; /* Set vector table location */
    SystemClock_Config();
    MX_GPIO_Init();
    __enable_irq(); /* Enable global interrupts */

    log_uart_init(LOG_LEVEL_ALL);

    log_info("\n\n");
    log_info("====================================");
    log_info("  Producer-Consumer Queue Demo");
    log_info("====================================");
    log_info("Simulating sensor data processing");
    log_info("- 3 sensor producers");
    log_info("- 2 data consumers");
    log_info("- Queue-based communication");
    log_info("====================================\n");

    /* Initialize RTOS */
    status = rtos_init();
    if (status != RTOS_SUCCESS)
    {
        log_error("RTOS init failed: %d", status);
        indicate_system_failure();
    }

    log_info("RTOS initialized successfully");

    /* Create sensor data queue (capacity: 5 readings) */
    status = rtos_queue_create(&g_sensor_queue, 5, sizeof(sensor_data_t));
    if (status != RTOS_SUCCESS)
    {
        log_error("Queue creation failed: %d", status);
        indicate_system_failure();
    }

    log_info("Sensor queue created (capacity: 5)");

    /* Create task handles */
    rtos_task_handle_t task_handle;

    /* Create producer tasks (sensors) */
    log_info("Creating sensor tasks...");

    status = rtos_task_create(temp_sensor_1_task, "TEMP1", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TEMP_SENSOR_1_PRIORITY,
                              &task_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    status = rtos_task_create(temp_sensor_2_task, "TEMP2", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, TEMP_SENSOR_2_PRIORITY,
                              &task_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    status = rtos_task_create(pressure_sensor_task, "PRESS", RTOS_DEFAULT_TASK_STACK_SIZE, NULL,
                              PRESSURE_SENSOR_PRIORITY, &task_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    /* Create consumer tasks (data processing) */
    log_info("Creating processor tasks...");

    status = rtos_task_create(data_processor_task, "PROC", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, DATA_PROCESSOR_PRIORITY,
                              &task_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    status =
        rtos_task_create(display_task, "DISP", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, DISPLAY_TASK_PRIORITY, &task_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    /* Create monitoring tasks */
    log_info("Creating monitoring tasks...");

    status =
        rtos_task_create(monitor_task, "MON", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, MONITOR_TASK_PRIORITY, &task_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    status =
        rtos_task_create(heartbeat_task, "HEART", RTOS_DEFAULT_TASK_STACK_SIZE, NULL, HEARTBEAT_PRIORITY, &task_handle);
    if (status != RTOS_SUCCESS)
        indicate_system_failure();

    log_info("\nAll tasks created successfully!");
    log_info("Starting RTOS scheduler...\n");

    /* Start the RTOS scheduler */
    status = rtos_start_scheduler();

    /* Should never reach here */
    log_error("Scheduler returned unexpectedly!");
    indicate_system_failure();

    while (1)
    {
    }
}

/* =================== HAL Configuration =================== */

__attribute__((__noreturn__)) void Error_Handler(void)
{
    __disable_irq();
    indicate_system_failure();
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Configure the main internal regulator output voltage */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

    /* Initialize the RCC Oscillators */
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /* Initialize the CPU, AHB and APB buses clocks */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin   = GPIO_PIN_5;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/* =================== Fault Handlers =================== */

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile("TST LR, #4              \n"
                   "ITE EQ                  \n"
                   "MRSEQ R0, MSP           \n"
                   "MRSNE R0, PSP           \n"
                   "MOV R1, R0              \n"
                   "B HardFault_Handler_C");
}

__attribute__((__noreturn__)) void HardFault_Handler_C(uint32_t *stack_frame)
{
    uint32_t r0  = stack_frame[0];
    uint32_t r1  = stack_frame[1];
    uint32_t r2  = stack_frame[2];
    uint32_t r3  = stack_frame[3];
    uint32_t r12 = stack_frame[4];
    uint32_t lr  = stack_frame[5];
    uint32_t pc  = stack_frame[6];
    uint32_t psr = stack_frame[7];

    log_error("HardFault: PC=0x%08lX PSR=0x%08lX", (unsigned long) pc, (unsigned long) psr);
    log_error("R0=0x%08lX R1=0x%08lX R2=0x%08lX R3=0x%08lX", (unsigned long) r0, (unsigned long) r1, (unsigned long) r2,
              (unsigned long) r3);
    log_error("R12=0x%08lX LR=0x%08lX", (unsigned long) r12, (unsigned long) lr);

    uint32_t cfsr  = SCB->CFSR;
    uint32_t hfsr  = SCB->HFSR;
    uint32_t mmfar = SCB->MMFAR;
    uint32_t bfar  = SCB->BFAR;
    uint32_t psp   = __get_PSP();
    uint32_t msp   = __get_MSP();

    log_error("CFSR=0x%08lX HFSR=0x%08lX", (unsigned long) cfsr, (unsigned long) hfsr);
    log_error("MMFAR=0x%08lX BFAR=0x%08lX", (unsigned long) mmfar, (unsigned long) bfar);
    log_error("PSP=0x%08lX MSP=0x%08lX", (unsigned long) psp, (unsigned long) msp);

    indicate_system_failure();
}
