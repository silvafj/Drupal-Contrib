<?php

/**
 * @file
 * Provides Rules integration for Commerce Coupon.
 */

/**
 * Implements hook_default_rules_configuration().
 */
function commerce_coupon_better_default_rules_configuration() {
  $rules = array();

  // Reaction on commerce coupon validate
  $rules['commerce_coupon_better_validate_coupon'] = rules_import('{ "commerce_coupon_better_validate_coupon" : {
      "LABEL" : "Disallow an invalid commerce coupon",
      "PLUGIN" : "reaction rule",
      "REQUIRES" : [ "commerce_coupon_better", "commerce_coupon" ],
      "ON" : [ "commerce_coupon_validate" ],
      "IF" : [
        { "NOT commerce_coupon_better_is_valid" : { "coupon" : [ "coupon" ], "order" : [ "commerce-order" ] } }
      ],
      "DO" : [ { "commerce_coupon_action_is_invalid_coupon" : [] } ]
    }
  }');

  // Reaction on commerce coupon redeem
  $rules['commerce_coupon_better_redeem_coupon'] = rules_import('{ "commerce_coupon_better_redeem_coupon" : {
      "LABEL" : "Apply coupon to the commerce order",
      "PLUGIN" : "reaction rule",
      "REQUIRES" : [ "commerce_coupon_better", "commerce_coupon" ],
      "ON" : [ "commerce_coupon_redeem" ],
      "DO" : [
        { "commerce_coupon_better_grant_coupon" : { "coupon" : [ "coupon" ], "order" : [ "commerce-order" ] } }
      ]
    }
  }');

  return $rules;
}
