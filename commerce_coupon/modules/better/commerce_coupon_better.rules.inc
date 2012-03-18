<?php

/**
 * @file
 * Provides Rules integration for Commerce Coupon.
 */

/**
 * Implements hook_rules_condition_info().
 */
function commerce_coupon_better_rules_condition_info() {
  $conditions['commerce_coupon_better_is_valid'] = array(
    'group' => t('Commerce Coupon'), 
    'label' => t('Coupon is valid'), 
    'parameter' => array(
      'coupon' => array(
        'type' => 'commerce_coupon',
        'label' => t('Coupon'),
      ),
      'order' => array(
        'type' => 'commerce_order', 
        'label' => t('Order'), 
      ), 
    ), 
  );

  return $conditions;
}

/**
 * Validates if the specified coupon is valid for the current order.
 */
function commerce_coupon_better_is_valid($coupon, $order = NULL) {
  global $user, $commerce_coupon_validation_error_message;

  // Make it easy to access entity properties
  $coupon_wrapper = entity_metadata_wrapper('commerce_coupon', $coupon);

  // Coupon has expired?
  if (time() > (int)$coupon_wrapper->commerce_coupon_expire->value()) {
    $commerce_coupon_validation_error_message = t('Your coupon code is expired.');
    return FALSE;
  }

  // Coupon reached number of uses?
  if (commerce_coupon_get_number_of_uses($coupon->coupon_id) >= (int)$coupon_wrapper->commerce_coupon_number_of_uses->value()) {
    $commerce_coupon_validation_error_message = t('Your coupon code reached the number of uses limit.');
    return FALSE;
  }

  // Check if the coupon defined products are present in the order
  if ($order) {
    $order_wrapper = entity_metadata_wrapper('commerce_order', $order);
    if ($allowed_products = $coupon_wrapper->commerce_coupon_product->value()) {
      $order_skus = array();
      foreach($order_wrapper->commerce_line_items->value() as $line_item) {
        $line_item = entity_metadata_wrapper('commerce_line_item', $line_item);
        $order_skus[] = $line_item->commerce_product->value()->sku;
      }

      $product_found = FALSE;
      foreach($allowed_products as $product) {
        $product_found = in_array($product->sku, $order_skus);
        if ($product_found) break;
      }

      if (!$product_found) {
        $commerce_coupon_validation_error_message = t('Your coupon code does not match any product in your order.');
        return FALSE;
      }
    }
  }

  // Check if the coupon is already used by the current authenticated user
  if ($user->uid && (int)$coupon_wrapper->commerce_coupon_usage->value()) {
    $coupon_log = db_select('commerce_coupon_log', 'c')
      ->fields('c', array('log_id'))
      ->condition('coupon_id', $coupon->coupon_id, '=')
      ->condition('uid', $user->uid, '=')
      ->execute()
      ->fetchAssoc();

    if ($coupon_log['log_id']) {
      $commerce_coupon_validation_error_message = t('Your coupon code was already used.');
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * Implements hook_rules_action_info().
 */
function commerce_coupon_better_rules_action_info() {
  $actions['commerce_coupon_better_grant_coupon'] = array(
    'group' => t('Commerce Coupon'),
    'label' => t('Grant coupon'),
    'parameter' => array(
      'coupon' => array(
        'type' => 'commerce_coupon',
        'label' => t('Coupon'),
      ),
      'order' => array(
        'type' => 'commerce_order', 
        'label' => t('Order'), 
      ), 
    ),
  );

  return $actions;
}

/**
 * Apply coupon conditions to the commerce order.
 */
function commerce_coupon_better_grant_coupon($coupon, $order) {
  // Make it easy to access entity properties
  $coupon_wrapper = entity_metadata_wrapper('commerce_coupon', $coupon);
  $allowed_products = array();
  foreach($coupon_wrapper->commerce_coupon_product->value() as $product) {
    $allowed_products[] = $product->sku;
  }

  // Iterate over all line items and calculate the coupon discount of each line
  $coupon_total = 0;
  $coupon_currency_code = NULL;
  $order = entity_metadata_wrapper('commerce_order', $order);
  foreach ($order->commerce_line_items as $index => $line_item) {
    // Don't process commerce line itens of type 'coupon'
    if ($line_item->type->value() == 'coupon') continue;

    // Check if the current coupon can be applied to the line
    if ($allowed_products && !in_array($line_item->commerce_product->sku->value(), $allowed_products)) continue;

    $line_amount = (int)$line_item->commerce_total->amount->value();
    $coupon_currency_code = $line_item->commerce_total->currency_code->value();

    // Discount a fixed amount
    if (($fixed_amount = (int)$coupon_wrapper->commerce_coupon_fixed_amount->amount->value()) > 0) {
      $coupon_total += $line_amount >= $fixed_amount ? $fixed_amount : $line_amount;
      $line_amount -= $fixed_amount;
    }

    // Discount a percentage
    if (($rate = $coupon_wrapper->commerce_coupon_percent_amount->value()) > 0) {
      // Ensure that the rate is never bigger then 100%
      $rate = $rate > 1 ? $rate / 100 : $rate;

      // Calculate the total granted amount for this line item
      $coupon_total += $line_amount * $rate;
    }

    // Keep a reference between the commerce product line to the coupon
    if ($line_item->commerce_coupon_reference != $coupon_wrapper->commerce_coupon_code->value()) {
      $line_item->commerce_coupon_reference = $coupon_wrapper->commerce_coupon_code->value();
      commerce_line_item_save($line_item->value());
    }
  }

  // Find the commerce coupon line item associated with this coupon
  $coupon_line_item = NULL;
  foreach ($order->commerce_line_items as $line_item_delta => $line_item) {
    if ($line_item->type->value() == 'coupon' && $line_item->commerce_coupon_reference->value() == $coupon->coupon_id) {
      $coupon_line_item = $line_item;
      break;
    }
  }

  // If the coupon does not apply to this order, remove it
  if ($coupon_total == 0) {
    if ($coupon_line_item) {
      unset($order->commerce_line_items[$line_item_delta]);
      $commerce_coupon_log = commerce_coupon_log_load_by_order_and_coupon($order->order_id->value(), $coupon->coupon_id);
      $commerce_coupon_log->delete();
    }
    return;
  }

  // Create a new commerce coupon line item (if none found)
  $is_new_coupon_line = FALSE;
  if (!$coupon_line_item) {
    $coupon_line_item = commerce_coupon_line_item_new($coupon, $order->order_id->value());
    $is_new_coupon_line = TRUE;
  }

  // Apply the coupon discount value to this line
  $coupon_line_item = entity_metadata_wrapper('commerce_line_item', $coupon_line_item);
  $coupon_line_item->commerce_unit_price->amount = $coupon_total * -1;
  $coupon_line_item->commerce_unit_price->currency_code = $coupon_currency_code;

  // Add the base price to the components array
  $coupon_line_item->commerce_unit_price->data = NULL;
  $coupon_line_item->commerce_unit_price->data = commerce_price_component_add(
    $coupon_line_item->commerce_unit_price->value(),
    'commerce_coupon_' . $coupon->type,
    $coupon_line_item->commerce_unit_price->value(),
    TRUE, FALSE);

  // Persist to database 
  commerce_line_item_save($coupon_line_item->value());
  if ($is_new_coupon_line) {
    $order->commerce_line_items[] = $coupon_line_item->value();
    $order->save();
  }

  // Update coupon log granted amount
  $commerce_coupon_log = commerce_coupon_log_load_by_order_and_coupon($order->order_id->value(), $coupon->coupon_id);
  $commerce_coupon_log = entity_metadata_wrapper('commerce_coupon_log', $commerce_coupon_log);

  $commerce_coupon_log->commerce_granted_amount->amount = $coupon_total;
  if ($coupon_currency_code) {
    $commerce_coupon_log->commerce_granted_amount->currency_code = $coupon_currency_code;
  }

  // Add the base price to the components array.
  if (!commerce_price_component_load($commerce_coupon_log->commerce_granted_amount->value(), 'commerce_coupon_' . $coupon->type)) {
    $commerce_coupon_log->commerce_granted_amount->data = commerce_price_component_add(
      $commerce_coupon_log->commerce_granted_amount->value(),
      'commerce_coupon_' . $coupon->type,
      $commerce_coupon_log->commerce_granted_amount->value(),
      TRUE,
      FALSE
    );
  }

  commerce_coupon_log_save($commerce_coupon_log->value());
}
