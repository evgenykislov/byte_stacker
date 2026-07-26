//
//  ViewController.swift
//  stacker
//
//  Created by Евгений on 19.07.2026.
//

import UIKit

class ViewController: UIViewController {

  var run_state_: Bool

  required init?(coder c: NSCoder) {
    run_state_ = false
    super.init(coder: c)
  }
  
  @IBOutlet weak var run_btn_: UIButton!
  @IBOutlet weak var stop_btn_: UIButton!
    
    
  override func viewDidLoad() {
    super.viewDidLoad()

    Update()
  }


  @IBAction func OnRun(_ sender: UIButton) {
    run_stacker_in()
    run_state_ = true
    Update()
  }
    
  @IBAction func OnStop(_ sender: UIButton) {
    run_state_ = false
    Update()
  }
  
  func Update() {
    run_btn_.isEnabled = !run_state_
    stop_btn_.isEnabled = run_state_
  }
}

