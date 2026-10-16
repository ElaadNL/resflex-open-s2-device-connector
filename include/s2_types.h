// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#define S2_NODE_MAX_SUPPORTED_VERSIONS 5

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief S2 node role
 */
typedef enum {
    S2_ROLE_CEM,
    S2_ROLE_RM
} s2_role;

/**
 * @brief S2 commodity quantity
 *
 * Combines a commodity (ELECTRICITY, WATER, GAS, HEAT, OIL) with their corresponding power quantity/phase.
 * Used to identify which phase or commodity a power value, limit or measurement refers to
 * according to S2 spec. 
 * 
 * See spec:
 * Commodities: https://docs.s2standard.org/model-reference/Common/Commodity/
 * Commodity Quantities: https://docs.s2standard.org/model-reference/Common/CommodityQuantity/
 *
 */
typedef enum {
    S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1,                /*!< Electric power on L1 (or single-phase), in Watt */
    S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L2,                /*!< Electric power on L2, in Watt. 3-phase only */
    S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L3,                /*!< Electric power on L3, in Watt. 3-phase only */
    S2_COMMODITY_QUANTITY_ELECTRIC_POWER_3_PHASE_SYMMETRIC, /*!< Total 3-phase symmetric power, in Watt */
    S2_COMMODITY_QUANTITY_NATURAL_GAS_FLOW_RATE,            /*!< Natural gas flow rate, in liters per second */
    S2_COMMODITY_QUANTITY_HYDROGEN_FLOW_RATE,               /*!< Hydrogen gas flow rate, in grams per second */
    S2_COMMODITY_QUANTITY_HEAT_THERMAL_POWER,               /*!< Thermal power, in Watt */
    S2_COMMODITY_QUANTITY_HEAT_FLOW_RATE,                   /*!< Heat carrier flow rate, in liters per second */
    S2_COMMODITY_QUANTITY_HEAT_TEMPERATURE,                 /*!< Heat, in degrees Celsius */
    S2_COMMODITY_QUANTITY_OIL_FLOW_RATE,                    /*!< Oil flow rate, in liters per hour */
} s2_commodity_quantity_t;

/**
 * @brief S2 commodity
 *
 * All possible commodities that an S2 compatible device can consume/produce/store.
 * See: https://docs.s2standard.org/model-reference/Common/Commodity/
 */
typedef enum {
    S2_COMMODITY_ELECTRICITY, /*!< Electrical energy */
    S2_COMMODITY_GAS,         /*!< Gas (natural gas or hydrogen) */
    S2_COMMODITY_HEAT,        /*!< Thermal energy */
    S2_COMMODITY_OIL,         /*!< Oil */
} s2_commodity_t;

/**
 * @brief S2 role type
 *
 * Whether a device consumes, produces or stores a commodity.
 * See: https://docs.s2standard.org/model-reference/Common/RoleType/
 */
typedef enum {
    S2_ROLE_TYPE_ENERGY_CONSUMER, 
    S2_ROLE_TYPE_ENERGY_PRODUCER,
    S2_ROLE_TYPE_ENERGY_STORAGE,
} s2_role_type_t;

/**
 * @brief S2 Role
 *
 * Object that holds the commodity and role type of device. See above for commodity and role type definitions.
 * See: https://docs.s2standard.org/model-reference/Common/Role/
 */
typedef struct {
    s2_commodity_t commodity; 
    s2_role_type_t role_type;
} s2_role_t;

/**
 * @brief S2 node deployment type
 */
typedef enum {
    S2_DEPLOYMENT_WAN,
    S2_DEPLOYMENT_LAN
} s2_deployment;

/**
 * @brief S2 node information
 */
typedef struct {
    char id[37];                                                     /*!< Unique identifier for the client node (e.g. UUID) */
    char brand[64];
    char logo_uri[128];
    char type[64];
    char model[64];
    char user_defined_name[64];
    s2_role role;
    s2_deployment deployment;
    char supported_versions[S2_NODE_MAX_SUPPORTED_VERSIONS][32];
} s2_node_info_t;

#ifdef __cplusplus
}
#endif
